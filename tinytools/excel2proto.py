#!/usr/bin/env python3
"""
excel2proto.py - Excel 配置表转 Protobuf 序列化工具

读取含 Schema 信息的 Excel 文件，生成双套（server/client）Protobuf 定义，
编译出 Python 和 C++ 代码，并将 Excel 数据序列化为 .bin 和 .textproto 文件。

Excel Schema 规范:
  - 枚举 Sheet: 名称以 _enum_ 开头（如 _enum_JobType），列为 name|value
  - 数据 Sheet: 前4行为 schema，第5行起为数据
    第1行: 字段名
    第2行: 类型（int32/string/float/bool/EnumName/repeated X）
    第3行: tag(s:c)，如 1:101，0表示该侧不需要，简写5=5:5
    第4行: 说明（fk:子表字段名 表示外键关联）

用法:
    python excel2proto.py <input.xlsx> --protoc <path> [options]

示例:
    python excel2proto.py config.xlsx --protoc /usr/bin/protoc --outdir ./output
    python excel2proto.py config.xlsx --protoc protoc --pkg myconfig --outdir ./gen

# 默认用 grpc_tools（无需独立 protoc）
python excel2proto.py config.xlsx --outdir ./output

# 指定独立 protoc（额外生成 C++ 代码）
python excel2proto.py config.xlsx --protoc /path/to/protoc --outdir ./output

"""

import argparse
import importlib
import importlib.util
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from typing import List, Dict, Tuple, Optional, Any

try:
    from openpyxl import load_workbook
except ImportError:
    print("错误: 需要安装 openpyxl: pip install openpyxl")
    sys.exit(1)


# ===========================================================================
# 数据结构定义
# ===========================================================================

@dataclass
class EnumValue:
    name: str
    value: int


@dataclass
class EnumDef:
    """枚举定义。"""
    name: str               # 枚举名（如 JobType）
    values: List[EnumValue] = field(default_factory=list)


@dataclass
class FieldDef:
    """字段定义。"""
    name: str               # 字段名
    raw_type: str           # 原始类型字符串
    server_tag: int         # 服务器 tag（0=不需要）
    client_tag: int         # 客户端 tag（0=不需要）
    desc: str               # 说明

    # 解析后的类型信息
    is_repeated: bool = False
    is_enum: bool = False
    is_message: bool = False
    is_scalar: bool = False
    type_name: str = ''     # 纯类型名（去掉 repeated 后）
    proto_type: str = ''    # proto 中写的类型字符串
    fk_field: str = ''      # 外键字段名（repeated message 时）

    def parse_type(self):
        """解析原始类型字符串，填充各标志。"""
        t = self.raw_type.strip()
        if t.startswith('repeated '):
            self.is_repeated = True
            t = t[len('repeated '):].strip()
        self.type_name = t

        # 标量类型集合
        scalar_types = {
            'int32', 'int64', 'uint32', 'uint64',
            'sint32', 'sint64', 'fixed32', 'fixed64',
            'sfixed32', 'sfixed64', 'float', 'double',
            'bool', 'string', 'bytes'
        }
        if t in scalar_types:
            self.is_scalar = True
            self.proto_type = t
        else:
            # 非标量，可能是 enum 或 message，运行时由 Schema 解析后确定
            self.proto_type = t


@dataclass
class SheetDef:
    """数据 Sheet 定义。"""
    sheet_name: str         # Excel sheet 名
    message_name: str       # proto message 名
    fields: List[FieldDef] = field(default_factory=list)
    data_rows: List[List[Any]] = field(default_factory=list)  # 数据行（按字段顺序）


@dataclass
class Schema:
    """整个 Excel 的解析结果。"""
    enums: List[EnumDef] = field(default_factory=list)
    sheets: List[SheetDef] = field(default_factory=list)
    # 辅助查找
    enum_names: set = field(default_factory=set)
    message_names: set = field(default_factory=set)

    def resolve_types(self):
        """解析完所有 sheet 后，确定每个字段的 is_enum/is_message。"""
        self.enum_names = {e.name for e in self.enums}
        self.message_names = {s.message_name for s in self.sheets}
        for sheet in self.sheets:
            for f in sheet.fields:
                if not f.is_scalar:
                    if f.type_name in self.enum_names:
                        f.is_enum = True
                    elif f.type_name in self.message_names:
                        f.is_message = True
                    else:
                        raise ValueError(
                            f"[{sheet.sheet_name}] 字段 '{f.name}' 的类型 "
                            f"'{f.type_name}' 不是标量、枚举或已定义的 message"
                        )


# ===========================================================================
# tag(s:c) 解析与校验
# ===========================================================================

def parse_tag(raw_tag: str, sheet_name: str, field_name: str) -> Tuple[int, int]:
    """
    解析 tag 字符串，返回 (server_tag, client_tag)。
    格式: "s:c" 或 "s"（简写，等价于 s:s）
    """
    raw_tag = str(raw_tag).strip()
    if not raw_tag:
        raise ValueError(f"[{sheet_name}] 字段 '{field_name}' 的 tag 为空")

    if ':' in raw_tag:
        parts = raw_tag.split(':')
        if len(parts) != 2:
            raise ValueError(
                f"[{sheet_name}] 字段 '{field_name}' 的 tag '{raw_tag}' "
                f"格式错误，应为 's:c'，如 1:101"
            )
        s_str, c_str = parts
    else:
        s_str = raw_tag
        c_str = raw_tag  # 简写 s = s:s

    # 校验非负整数
    for label, val_str in [('server', s_str), ('client', c_str)]:
        val_str = val_str.strip()
        if not val_str:
            raise ValueError(
                f"[{sheet_name}] 字段 '{field_name}' 的 tag '{raw_tag}' "
                f"中 {label} 侧为空"
            )
        if not re.match(r'^\d+$', val_str):
            raise ValueError(
                f"[{sheet_name}] 字段 '{field_name}' 的 tag '{raw_tag}' "
                f"中 {label} 侧 '{val_str}' 不是非负整数"
            )

    s_tag = int(s_str)
    c_tag = int(c_str)

    # 禁止 0:0
    if s_tag == 0 and c_tag == 0:
        raise ValueError(
            f"[{sheet_name}] 字段 '{field_name}' 的 tag 为 '0:0'，"
            f"两侧均不需要此字段，请删除该列"
        )

    return s_tag, c_tag


def validate_tags(schema: Schema):
    """校验所有 sheet 内 tag 不重复（分 server/client 两侧）。"""
    for sheet in schema.sheets:
        s_tags = {}
        c_tags = {}
        for f in sheet.fields:
            if f.server_tag > 0:
                if f.server_tag in s_tags:
                    raise ValueError(
                        f"[{sheet.sheet_name}] server tag {f.server_tag} 重复: "
                        f"字段 '{s_tags[f.server_tag]}' 和 '{f.name}'"
                    )
                s_tags[f.server_tag] = f.name
            if f.client_tag > 0:
                if f.client_tag in c_tags:
                    raise ValueError(
                        f"[{sheet.sheet_name}] client tag {f.client_tag} 重复: "
                        f"字段 '{c_tags[f.client_tag]}' 和 '{f.name}'"
                    )
                c_tags[f.client_tag] = f.name


# ===========================================================================
# Excel 解析
# ===========================================================================

def parse_excel(filepath: str) -> Schema:
    """读取 Excel 文件，解析 schema 和数据。"""
    wb = load_workbook(filepath, data_only=True, read_only=True)
    schema = Schema()

    for ws in wb.worksheets:
        sheet_name = ws.title

        if sheet_name.startswith('_enum_'):
            # 枚举 sheet
            enum_name = sheet_name[len('_enum_'):]
            enum_def = parse_enum_sheet(ws, sheet_name, enum_name)
            schema.enums.append(enum_def)
        elif sheet_name.startswith('_'):
            # 其他以 _ 开头的 sheet 跳过（如 _schema, _说明 等）
            continue
        else:
            # 数据 sheet
            sheet_def = parse_data_sheet(ws, sheet_name)
            if sheet_def:
                schema.sheets.append(sheet_def)

    wb.close()

    if not schema.sheets:
        raise ValueError("Excel 中没有找到数据 Sheet（不以 _ 开头的 sheet）")

    schema.resolve_types()
    validate_tags(schema)
    return schema


def parse_enum_sheet(ws, sheet_name: str, enum_name: str) -> EnumDef:
    """解析枚举 sheet。第一行为表头 name|value，后续为枚举项。"""
    enum_def = EnumDef(name=enum_name)
    rows = list(ws.iter_rows(values_only=True))

    if len(rows) < 1:
        raise ValueError(f"[{sheet_name}] 枚举 sheet 为空")

    # 跳过表头行，从第2行开始
    has_zero = False
    for row in rows[1:]:
        if row is None or all(c is None for c in row):
            continue
        name = str(row[0]).strip() if row[0] is not None else ''
        value = row[1] if len(row) > 1 and row[1] is not None else None
        if not name:
            continue
        if value is None:
            raise ValueError(f"[{sheet_name}] 枚举项 '{name}' 缺少 value")
        val = int(value)
        enum_def.values.append(EnumValue(name=name, value=val))
        if val == 0:
            has_zero = True

    if not enum_def.values:
        raise ValueError(f"[{sheet_name}] 枚举没有定义任何值")

    # proto3 要求第一个值必须为 0
    if not has_zero:
        raise ValueError(
            f"[{sheet_name}] 枚举 {enum_name} 缺少 value=0 的项，"
            f"proto3 要求枚举第一个值必须为 0"
        )

    return enum_def


def parse_data_sheet(ws, sheet_name: str) -> Optional[SheetDef]:
    """解析数据 sheet。前4行为 schema，第5行起为数据。"""
    rows = list(ws.iter_rows(values_only=True))
    if len(rows) < 4:
        # 少于4行的 sheet 跳过
        return None

    row_names = rows[0]    # 第1行: 字段名
    row_types = rows[1]    # 第2行: 类型
    row_tags = rows[2]     # 第3行: tag(s:c)
    row_descs = rows[3]    # 第4行: 说明

    message_name = sheet_name
    sheet_def = SheetDef(sheet_name=sheet_name, message_name=message_name)

    # 解析字段
    col_count = 0
    for i, cell in enumerate(row_names):
        if cell is None:
            break
        fname = str(cell).strip()
        if not fname:
            break
        col_count = i + 1

        ftype = str(row_types[i]).strip() if i < len(row_types) and row_types[i] else ''
        ftag_raw = str(row_tags[i]).strip() if i < len(row_tags) and row_tags[i] else ''
        fdesc = str(row_descs[i]).strip() if i < len(row_descs) and row_descs[i] else ''

        if not ftype:
            raise ValueError(f"[{sheet_name}] 字段 '{fname}' 缺少类型定义")

        s_tag, c_tag = parse_tag(ftag_raw, sheet_name, fname)

        fd = FieldDef(name=fname, raw_type=ftype, server_tag=s_tag,
                      client_tag=c_tag, desc=fdesc)
        fd.parse_type()

        # 解析外键（不依赖 is_message，因为此时还未 resolve_types）
        if fd.is_repeated and fdesc.startswith('fk:'):
            fd.fk_field = fdesc[len('fk:'):].strip()

        sheet_def.fields.append(fd)

    if not sheet_def.fields:
        return None

    # 读取数据行（第5行起）
    for row in rows[4:]:
        if row is None:
            continue
        if all(c is None for c in row[:col_count]):
            continue
        # 截取有效列数
        data = list(row[:col_count])
        # 补齐不足的列
        while len(data) < col_count:
            data.append(None)
        sheet_def.data_rows.append(data)

    return sheet_def


# ===========================================================================
# Proto 文件生成
# ===========================================================================

def generate_proto(schema: Schema, side: str, package: str) -> str:
    """
    生成 .proto 文件内容。
    side: 'server' 或 'client'
    package: 包名（如 config_server）
    """
    lines = []
    lines.append('syntax = "proto3";')
    lines.append(f'package {package};')
    lines.append('')

    # 生成 enum 定义
    for enum_def in schema.enums:
        lines.append(f'enum {enum_def.name} {{')
        for v in enum_def.values:
            lines.append(f'  {v.name} = {v.value};')
        lines.append('}')
        lines.append('')

    # 生成 message 定义
    for sheet in schema.sheets:
        lines.append(f'message {sheet.message_name} {{')
        for f in sheet.fields:
            tag = f.server_tag if side == 'server' else f.client_tag
            if tag == 0:
                continue  # 该侧不需要此字段
            prefix = 'repeated ' if f.is_repeated else ''
            lines.append(f'  {prefix}{f.proto_type} {f.name} = {tag};')
        lines.append('}')
        lines.append('')

    # 生成顶层 wrapper message（包含所有表的 repeated 字段，方便整体加载）
    wrapper_name = package.replace('_', ' ').title().replace(' ', '')
    lines.append(f'message {wrapper_name}All {{')
    for idx, sheet in enumerate(schema.sheets, 1):
        lines.append(f'  repeated {sheet.message_name} {sheet.message_name.lower()}_list = {idx};')
    lines.append('}')
    lines.append('')

    return '\n'.join(lines)


# ===========================================================================
# protoc 编译
# ===========================================================================

def run_protoc(proto_path: str, outdir: str, protoc: str,
               package_server: str, package_client: str) -> bool:
    """
    编译 proto 文件。
    - Python 代码: 始终用 grpc_tools.protoc 生成
    - C++ 代码: 如果 protoc 参数指向独立 protoc（非 'grpc_tools'），则用它生成
    """
    proto_dir = os.path.dirname(os.path.abspath(proto_path))
    has_cpp_protoc = protoc and protoc != 'grpc_tools'

    for package in [package_server, package_client]:
        proto_file = os.path.join(proto_dir, f'{package}.proto')
        if not os.path.exists(proto_file):
            print(f"错误: proto 文件不存在: {proto_file}")
            return False

        # 1. 生成 Python 代码（grpc_tools.protoc）
        py_cmd = [
            sys.executable, '-m', 'grpc_tools.protoc',
            f'--proto_path={proto_dir}',
            f'--python_out={outdir}',
            proto_file
        ]
        try:
            result = subprocess.run(py_cmd, capture_output=True, timeout=60)
            if result.returncode != 0:
                stderr = result.stderr
                if isinstance(stderr, bytes):
                    stderr = stderr.decode('utf-8', errors='replace')
                print(f"错误: grpc_tools.protoc 编译 {package}.proto 失败:")
                print(stderr)
                return False
        except subprocess.TimeoutExpired:
            print(f"错误: grpc_tools.protoc 编译 {package}.proto 超时")
            return False

        # 2. 生成 C++ 代码（如果有独立 protoc）
        if has_cpp_protoc:
            cpp_cmd = [
                protoc,
                f'--proto_path={proto_dir}',
                f'--cpp_out={outdir}',
                proto_file
            ]
            try:
                result = subprocess.run(cpp_cmd, capture_output=True, timeout=60)
                if result.returncode != 0:
                    stderr = result.stderr
                    if isinstance(stderr, bytes):
                        stderr = stderr.decode('utf-8', errors='replace')
                    print(f"警告: protoc 生成 C++ 代码失败 ({package}.proto):")
                    print(stderr)
                    # C++ 失败不中断，Python 序列化仍可继续
            except FileNotFoundError:
                print(f"警告: 找不到 protoc: {protoc}，跳过 C++ 代码生成")
            except subprocess.TimeoutExpired:
                print(f"警告: protoc 生成 C++ 代码超时，跳过")

    return True


# ===========================================================================
# 数据序列化
# ===========================================================================

def convert_scalar(value, proto_type: str, sheet_name: str, field_name: str):
    """将 Excel 单元格值转为标量 proto 值。"""
    if value is None or str(value).strip() == '':
        # 返回默认值
        if proto_type in ('int32', 'int64', 'uint32', 'uint64',
                          'sint32', 'sint64', 'fixed32', 'fixed64',
                          'sfixed32', 'sfixed64'):
            return 0
        elif proto_type in ('float', 'double'):
            return 0.0
        elif proto_type == 'bool':
            return False
        elif proto_type == 'string':
            return ''
        else:
            return 0

    sval = str(value).strip()

    if proto_type in ('int32', 'int64', 'uint32', 'uint64',
                      'sint32', 'sint64', 'fixed32', 'fixed64',
                      'sfixed32', 'sfixed64'):
        return int(sval)
    elif proto_type in ('float', 'double'):
        return float(sval)
    elif proto_type == 'bool':
        sl = sval.lower()
        if sl in ('true', '1', 'yes', '是'):
            return True
        elif sl in ('false', '0', 'no', '否'):
            return False
        else:
            raise ValueError(f"[{sheet_name}] 字段 '{field_name}' 的 bool 值 '{sval}' 无法识别")
    elif proto_type == 'string':
        return sval
    else:
        return sval


def convert_enum(value, enum_def: EnumDef, sheet_name: str, field_name: str) -> int:
    """将 Excel 中的 enum 值（名称或数字）转为枚举数值。"""
    if value is None or str(value).strip() == '':
        return 0

    sval = str(value).strip()

    # 先尝试按名称匹配
    for v in enum_def.values:
        if v.name == sval:
            return v.value

    # 再尝试按数字匹配
    try:
        num = int(sval)
        for v in enum_def.values:
            if v.value == num:
                return num
    except ValueError:
        pass

    raise ValueError(
        f"[{sheet_name}] 字段 '{field_name}' 的枚举值 '{sval}' "
        f"不在枚举 {enum_def.name} 中"
    )


def convert_repeated_scalar(value, proto_type: str, sheet_name: str, field_name: str) -> list:
    """将 Excel 单元格中的分隔符列表转为标量列表。"""
    if value is None or str(value).strip() == '':
        return []
    sval = str(value).strip()
    parts = [p.strip() for p in sval.split(';') if p.strip()]
    result = []
    for p in parts:
        if proto_type in ('int32', 'int64', 'uint32', 'uint64',
                          'sint32', 'sint64', 'fixed32', 'fixed64',
                          'sfixed32', 'sfixed64'):
            result.append(int(p))
        elif proto_type in ('float', 'double'):
            result.append(float(p))
        elif proto_type == 'bool':
            result.append(p.lower() in ('true', '1', 'yes', '是'))
        elif proto_type == 'string':
            result.append(p)
        else:
            result.append(p)
    return result


def build_data_index(schema: Schema) -> Dict[str, Dict[Any, Any]]:
    """
    为每个 sheet 建立以 id 字段为 key 的数据索引。
    返回: {message_name: {id_value: proto_message}}
    先把所有 sheet 的数据填充为 proto message（不含外键嵌套），供外键关联使用。
    """
    # 这个函数在 serialize_all 中被调用，需要 proto module
    # 实际实现见 serialize_all
    pass


def serialize_all(schema: Schema, outdir: str, proto_dir: str,
                  package_server: str, package_client: str):
    """
    序列化所有 sheet 数据，输出 .bin 和 .textproto 文件。
    分 server 和 client 两套。
    """
    from google.protobuf import text_format

    for side, package in [('server', package_server), ('client', package_client)]:
        # 动态加载编译后的 pb2 模块
        pb2_module = load_pb2_module(package, proto_dir)

        # 枚举查找表
        enum_map = {e.name: e for e in schema.enums}

        # 第一遍：为每个 sheet 创建 proto message 列表（不含外键嵌套）
        # message_index[message_name] = {id_value: message_instance}
        message_index = {}
        # message_lists[message_name] = [message_instance, ...]
        message_lists = {}

        for sheet in schema.sheets:
            msg_class = getattr(pb2_module, sheet.message_name)
            msg_list = []
            id_index = {}

            for row in sheet.data_rows:
                msg = msg_class()
                for i, fdef in enumerate(sheet.fields):
                    tag = fdef.server_tag if side == 'server' else fdef.client_tag
                    if tag == 0:
                        continue  # 该侧不需要此字段

                    cell_val = row[i] if i < len(row) else None

                    if fdef.is_scalar and not fdef.is_repeated:
                        val = convert_scalar(cell_val, fdef.proto_type,
                                             sheet.sheet_name, fdef.name)
                        setattr(msg, fdef.name, val)
                    elif fdef.is_scalar and fdef.is_repeated:
                        vals = convert_repeated_scalar(cell_val, fdef.proto_type,
                                                       sheet.sheet_name, fdef.name)
                        getattr(msg, fdef.name).extend(vals)
                    elif fdef.is_enum and not fdef.is_repeated:
                        enum_def = enum_map.get(fdef.proto_type)
                        if not enum_def:
                            raise ValueError(f"找不到枚举定义: {fdef.proto_type}")
                        val = convert_enum(cell_val, enum_def,
                                           sheet.sheet_name, fdef.name)
                        setattr(msg, fdef.name, val)
                    elif fdef.is_enum and fdef.is_repeated:
                        enum_def = enum_map.get(fdef.proto_type)
                        if not enum_def:
                            raise ValueError(f"找不到枚举定义: {fdef.proto_type}")
                        if cell_val is None or str(cell_val).strip() == '':
                            continue
                        parts = [p.strip() for p in str(cell_val).split(';') if p.strip()]
                        for p in parts:
                            val = convert_enum(p, enum_def,
                                               sheet.sheet_name, fdef.name)
                            getattr(msg, fdef.name).append(val)
                    elif fdef.is_message and not fdef.is_repeated:
                        # 单个嵌套 message（暂不支持从单元格直接解析）
                        pass
                    elif fdef.is_message and fdef.is_repeated:
                        # repeated message 通过外键关联，第二遍处理
                        pass

                msg_list.append(msg)
                # 建立主键索引（约定第一个字段为主键）
                if sheet.fields:
                    pk_field = sheet.fields[0].name
                    pk_val = getattr(msg, pk_field, None)
                    if pk_val is not None:
                        id_index[pk_val] = msg

            message_lists[sheet.message_name] = msg_list
            message_index[sheet.message_name] = id_index

        # 第二遍：处理外键关联（repeated message）
        for sheet in schema.sheets:
            for fdef in sheet.fields:
                if not (fdef.is_repeated and fdef.is_message and fdef.fk_field):
                    continue
                tag = fdef.server_tag if side == 'server' else fdef.client_tag
                if tag == 0:
                    continue

                # 找到子表
                child_sheet = None
                for s in schema.sheets:
                    if s.message_name == fdef.type_name:
                        child_sheet = s
                        break
                if not child_sheet:
                    continue

                # 子表数据索引
                child_list = message_lists.get(child_sheet.message_name, [])

                # fk_field 是子表中的外键字段
                # 遍历父表每条记录，找子表中 fk_field == 父表主键 的记录
                pk_field = sheet.fields[0].name if sheet.fields else 'id'
                for parent_msg in message_lists[sheet.message_name]:
                    parent_id = getattr(parent_msg, pk_field, None)
                    if parent_id is None:
                        continue
                    for child_msg in child_list:
                        fk_val = getattr(child_msg, fdef.fk_field, None)
                        if fk_val == parent_id:
                            # 将子 message 的副本加入父 message
                            new_child = getattr(parent_msg, fdef.name).add()
                            new_child.CopyFrom(child_msg)

        # 输出每张表的 .bin 和 .textproto
        for sheet in schema.sheets:
            msg_list = message_lists[sheet.message_name]

            # 包装成 wrapper message 输出
            wrapper_name = package.replace('_', ' ').title().replace(' ', '') + 'All'
            wrapper_class = getattr(pb2_module, wrapper_name, None)

            if wrapper_class:
                wrapper = wrapper_class()
                field_name = sheet.message_name.lower() + '_list'
                getattr(wrapper, field_name).extend(msg_list)

                # 输出 wrapper bin
                bin_path = os.path.join(outdir, f'{sheet.message_name}_{side}.bin')
                with open(bin_path, 'wb') as f:
                    f.write(wrapper.SerializeToString())

                # 输出 wrapper textproto
                text_path = os.path.join(outdir, f'{sheet.message_name}_{side}.textproto')
                with open(text_path, 'w', encoding='utf-8') as f:
                    f.write(text_format.MessageToString(wrapper))
            else:
                # 没有 wrapper，直接逐条输出
                bin_path = os.path.join(outdir, f'{sheet.message_name}_{side}.bin')
                with open(bin_path, 'wb') as f:
                    for msg in msg_list:
                        f.write(msg.SerializeToString())

                text_path = os.path.join(outdir, f'{sheet.message_name}_{side}.textproto')
                with open(text_path, 'w', encoding='utf-8') as f:
                    for msg in msg_list:
                        f.write(text_format.MessageToString(msg))
                        f.write('\n---\n')

        print(f"[{side}] 序列化完成: {len(schema.sheets)} 张表")


def load_pb2_module(package: str, proto_dir: str):
    """动态加载 protoc 编译生成的 _pb2.py 模块。"""
    module_name = f'{package}_pb2'
    module_path = os.path.join(proto_dir, f'{module_name}.py')

    if not os.path.exists(module_path):
        raise FileNotFoundError(f"找不到编译后的模块: {module_path}")

    spec = importlib.util.spec_from_file_location(module_name, module_path)
    module = importlib.util.module_from_spec(spec)
    # 确保 proto_dir 在 path 中
    if proto_dir not in sys.path:
        sys.path.insert(0, proto_dir)
    spec.loader.exec_module(module)
    return module


# ===========================================================================
# 主函数
# ===========================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Excel 配置表转 Protobuf 序列化工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Excel Schema 规范:
  枚举 Sheet: 名称以 _enum_ 开头（如 _enum_JobType），列为 name|value
  数据 Sheet 前4行为 schema:
    第1行: 字段名
    第2行: 类型（int32/string/float/bool/EnumName/repeated X）
    第3行: tag(s:c)，如 1:101，0表示该侧不需要，简写5=5:5
    第4行: 说明（fk:子表字段名 表示外键关联）

示例:
  python excel2proto.py config.xlsx --protoc /usr/bin/protoc --outdir ./output
  python excel2proto.py config.xlsx --protoc protoc --pkg myconfig --outdir ./gen
'''
    )
    parser.add_argument('input', help='输入 Excel 文件路径 (.xlsx)')
    parser.add_argument('--protoc', default='grpc_tools',
                        help='protoc 编译器路径，或 "grpc_tools" 使用 Python 内置 (默认: grpc_tools)')
    parser.add_argument('--outdir', default='./output',
                        help='输出目录 (默认: ./output)')
    parser.add_argument('--pkg', default='config',
                        help='包名前缀 (默认: config，生成 config_server/config_client)')

    args = parser.parse_args()

    # 检查输入文件
    if not os.path.exists(args.input):
        print(f"错误: 输入文件不存在: {args.input}")
        sys.exit(1)

    # 创建输出目录
    outdir = os.path.abspath(args.outdir)
    os.makedirs(outdir, exist_ok=True)

    package_server = f'{args.pkg}_server'
    package_client = f'{args.pkg}_client'

    print(f"输入文件: {args.input}")
    print(f"输出目录: {outdir}")
    print(f"包名: {package_server} / {package_client}")
    print()

    # === 第1步: 解析 Excel ===
    print(">>> 第1步: 解析 Excel Schema...")
    try:
        schema = parse_excel(args.input)
    except ValueError as e:
        print(f"解析错误: {e}")
        sys.exit(1)

    print(f"  枚举: {len(schema.enums)} 个")
    for e in schema.enums:
        print(f"    - {e.name}: {len(e.values)} 个值")
    print(f"  数据表: {len(schema.sheets)} 个")
    for s in schema.sheets:
        print(f"    - {s.message_name}: {len(s.fields)} 个字段, {len(s.data_rows)} 行数据")
    print()

    # === 第2步: 生成 .proto 文件 ===
    print(">>> 第2步: 生成 .proto 文件...")
    proto_server = generate_proto(schema, 'server', package_server)
    proto_client = generate_proto(schema, 'client', package_client)

    proto_server_path = os.path.join(outdir, f'{package_server}.proto')
    proto_client_path = os.path.join(outdir, f'{package_client}.proto')

    with open(proto_server_path, 'w', encoding='utf-8') as f:
        f.write(proto_server)
    with open(proto_client_path, 'w', encoding='utf-8') as f:
        f.write(proto_client)

    print(f"  生成: {package_server}.proto")
    print(f"  生成: {package_client}.proto")
    print()

    # === 第3步: 调用 protoc 编译 ===
    print(">>> 第3步: 编译 proto 文件...")
    if not run_protoc(proto_server_path, outdir, args.protoc,
                      package_server, package_client):
        print("编译失败，请检查 protoc 路径和 proto 文件")
        sys.exit(1)
    print(f"  生成 Python 模块: {package_server}_pb2.py, {package_client}_pb2.py")
    if args.protoc and args.protoc != 'grpc_tools':
        print(f"  生成 C++ 代码: {package_server}.pb.h/.cc, {package_client}.pb.h/.cc")
    else:
        print(f"  (C++ 代码需用独立 protoc 生成，请运行: protoc --cpp_out=. {package_server}.proto)")
    print()

    # === 第4步: 序列化数据 ===
    print(">>> 第4步: 序列化 Excel 数据...")
    try:
        serialize_all(schema, outdir, outdir, package_server, package_client)
    except Exception as e:
        print(f"序列化错误: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    print()

    # 汇总输出
    print("=" * 60)
    print("完成! 输出文件:")
    print("=" * 60)
    for f in sorted(os.listdir(outdir)):
        fpath = os.path.join(outdir, f)
        size = os.path.getsize(fpath)
        print(f"  {f:40s} {size:>8} bytes")
    print()


if __name__ == '__main__':
    main()
