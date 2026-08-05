#!/usr/bin/env python3
"""
xml2cpp.py - XML配置模板转C++代码生成工具

根据XML配置模板文件，自动生成C++头文件(.h)和源文件(.cpp)。
支持：嵌套类、vector、list、map、unordered_map、set、struct、enum
特性：构造函数、禁止拷贝、move语义、异常处理、bool loadXml、clear()

用法:
    python xml2cpp.py <template.xml> [--ns <namespace>] [--outdir <dir>] [--prefix <prefix>]

示例:
    python xml2cpp.py CampOfficialPromote.xml --ns xml --outdir ./output
"""

import xml.etree.ElementTree as ET
import sys
import os
import re
import argparse
from dataclasses import dataclass, field as dc_field
from typing import List, Optional, Tuple, Dict, Set

# ============================================================
# 常量定义
# ============================================================

BASIC_TYPES = {
    'uint8_t', 'int8_t', 'uint16_t', 'int16_t',
    'uint32_t', 'int32_t', 'uint64_t', 'int64_t',
    'float', 'double', 'bool', 'std::string',
}

TYPE_DEFAULTS = {
    'uint8_t': '0', 'int8_t': '0',
    'uint16_t': '0', 'int16_t': '0',
    'uint32_t': '0', 'int32_t': '0',
    'uint64_t': '0', 'int64_t': '0',
    'float': '0.0f', 'double': '0.0',
    'bool': 'false', 'std::string': '""',
}

# 用于 PascalCase 拆分的常见单词表
COMMON_WORDS = set('''
func function open close zone top list appoint official general camp promote
all any each both none auto manual fixed custom local global static const
round container key value name type id uid gid pid count num number max min
level exp rank score point star grade tier stage phase time date day hour sec
week month year timestamp color red green blue alpha white black yellow orange
purple status state flag mode kind sort order group category item reward cost
price gold silver copper diamond coin slot equip weapon armor shield helmet
boot glove ring skill buff debuff aura effect trigger condition target source
src dst dest from to send recv request response config setting option param
argument arg data info detail desc description comment note tip begin start end
stop pause resume reset clear init create destroy update remove delete add
insert push pop size len length capacity empty full index pos position offset
row col column line width height depth radius angle scale rotation path file
dir directory folder url uri link host port ip address addr user player char
character hero unit npc monster boss map world scene area region tile block
chunk battle fight attack defense defence hp mp sp cp damage heal resist dodge
crit critical speed range duration interval cooldown cd quest task mission goal
objective shop store market trade exchange buy sell friend guild clan team party
member mail message msg chat notify notice announce login logout register
connect disconnect version build patch update upgrade enable disable active
inactive visible hidden show hide lock unlock bind unbind attach detach enter
exit join leave visit return check test verify validate confirm true false yes
no on off success fail error warn warning info debug code reason cause result
output input total partial complete current next prev previous first last middle
left right bottom center front back inner outer up down north south east west
main sub extra special normal rare epic legend quality rarity limit cap default
base bonus rate ratio percent probability chance odds formula expression equation
table sheet record entry xml json csv binary root node leaf branch tree parent
child sibling tag attr attribute element text content load save dump parse
serialize deserialize get set has is can should will with without by for of in at
award building army soldier king queen prince princess lord lady knight guard
scout archer mage priest warrior assassin hunter druid paladin ranger monk
warlock necromancer resource energy power spirit strength agility intelligence
wisdom luck dodge block critical hit miss attack defense magic physical fire
water earth wind light dark poison ice lightning holy shadow nature dragon
castle city village town building tower wall gate bridge road forest mountain
river lake sea ocean desert snow rain weather season spring summer autumn winter
day night morning evening noon midnight dawn dusk
'''.split())

# ============================================================
# 单词拆分与命名转换
# ============================================================

def split_words(name: str) -> List[str]:
    """将名称拆分为单词列表。"""
    # 按下划线分割
    if '_' in name:
        return [w for w in name.split('_') if w]
    
    # 检查是否为纯大写缩写 (如 "XML", "ID")
    if name.isupper() and len(name) <= 5:
        return [name.lower()]
    
    # 按驼峰边界分割
    if name != name.lower():
        words = re.findall(r'[A-Z][a-z]*|[a-z]+|[0-9]+', name)
        if len(words) > 1:
            return [w.lower() for w in words]
        return [name.lower()]
    
    # 全小写：用字典贪心最长匹配
    words = []
    remaining = name.lower()
    while remaining:
        found = False
        for length in range(len(remaining), 1, -1):
            candidate = remaining[:length]
            if candidate in COMMON_WORDS:
                words.append(candidate)
                remaining = remaining[length:]
                found = True
                break
        if not found:
            # 剩余1-2字符且已有匹配的词，合并到前一个词（处理复数s等后缀）
            if len(remaining) <= 2 and words:
                words[-1] += remaining
            else:
                words.append(remaining)
            remaining = ''
    return words


def to_pascal_case(name: str) -> str:
    """转换为 PascalCase (如 "funcopen" -> "FuncOpen")。"""
    words = split_words(name)
    return ''.join(w.capitalize() for w in words)


def to_camel_case(name: str) -> str:
    """转换为 camelCase (如 "FuncOpen" -> "funcOpen")。"""
    words = split_words(name)
    if not words:
        return name
    result = words[0].lower()
    for w in words[1:]:
        result += w.capitalize()
    return result


# ============================================================
# 数据结构定义
# ============================================================

@dataclass
class Field:
    """简单字段（XML属性）。"""
    xml_name: str           # XML属性名 (如 "officialid")
    member_name: str        # C++成员名 (如 "officialid_")
    cpp_type: str           # C++类型 (如 "uint32_t")
    default_val: str        # 默认值 (如 "0")
    is_enum: bool = False
    enum_name: str = ''     # 枚举类型名
    enum_underlying_type: str = 'uint32_t'  # 枚举底层类型


@dataclass
class ClassDef:
    """类定义。"""
    xml_name: str           # XML标签名
    class_name: str         # C++类名 (PascalCase)
    fields: List[Field] = dc_field(default_factory=list)
    children: List['ChildMember'] = dc_field(default_factory=list)
    set_key_member: str = ''  # 如果用于set容器，记录key字段成员名，用于生成 operator<


@dataclass
class ChildMember:
    """子元素成员。"""
    xml_name: str           # XML标签名
    member_name: str        # C++成员名 (与xml_name相同)
    class_name: str         # C++类名
    container: str = ''     # '', 'vector', 'list', 'map', 'unordered_map', 'set'
    key_xml_name: str = ''  # map/unordered_map的key属性名
    key_cpp_type: str = ''  # map/unordered_map的key C++类型
    class_def: Optional[ClassDef] = None
    is_struct_ref: bool = False  # 是否引用预定义struct
    key_is_external: bool = False  # key不在struct/class字段中，需从XML属性直接读取


@dataclass
class EnumDef:
    """枚举定义。"""
    name: str
    underlying_type: str = 'uint32_t'
    values: List[Tuple[str, str]] = dc_field(default_factory=list)  # (name, value)


@dataclass
class StructDef:
    """struct定义。"""
    name: str
    class_def: ClassDef = None


@dataclass
class ParseResult:
    """解析结果。"""
    root_class: ClassDef = None
    enums: List[EnumDef] = dc_field(default_factory=list)
    structs: List[StructDef] = dc_field(default_factory=list)


# ============================================================
# 模板解析
# ============================================================

DIRECTIVE_ATTRS = {'container_', 'key_', 'struct_'}


def parse_field(name: str, type_str: str, enums: List[EnumDef]) -> Field:
    """解析一个字段定义。"""
    member_name = name + '_'
    is_enum = type_str.startswith('enum:')
    enum_name = ''
    enum_underlying = 'uint32_t'
    cpp_type = type_str
    default_val = '0'

    if is_enum:
        enum_name = type_str[5:]
        cpp_type = enum_name
        # 查找枚举的底层类型
        for e in enums:
            if e.name == enum_name:
                enum_underlying = e.underlying_type
                break
        default_val = f'static_cast<{enum_name}>(0)'
    elif type_str in TYPE_DEFAULTS:
        default_val = TYPE_DEFAULTS[type_str]
    elif type_str.startswith('struct:'):
        # struct类型字段（较少见，作为属性引用struct）
        cpp_type = type_str[7:]
        default_val = '{}'
    else:
        # 未知类型，按uint32_t处理
        default_val = '0'

    return Field(
        xml_name=name,
        member_name=member_name,
        cpp_type=cpp_type,
        default_val=default_val,
        is_enum=is_enum,
        enum_name=enum_name,
        enum_underlying_type=enum_underlying,
    )


def parse_enum(elem) -> EnumDef:
    """解析枚举定义。"""
    name = elem.attrib.get('name', 'UnknownEnum')
    underlying = elem.attrib.get('type', 'uint32_t')
    values = []
    for child in elem:
        if child.tag == 'value':
            v_name = child.attrib.get('name', '')
            v_val = child.attrib.get('val', child.attrib.get('value', '0'))
            values.append((v_name, v_val))
    return EnumDef(name=name, underlying_type=underlying, values=values)


def parse_struct_def(elem, enums: List[EnumDef]) -> StructDef:
    """解析struct定义（顶层）。"""
    name = elem.attrib.get('name', 'UnknownStruct')
    class_name = name  # struct名已是大写形式
    fields = []
    children = []

    for attr_name, attr_type in elem.attrib.items():
        if attr_name == 'name':
            continue
        if attr_name in DIRECTIVE_ATTRS:
            continue
        fields.append(parse_field(attr_name, attr_type, enums))

    class_def = ClassDef(xml_name=name, class_name=class_name, fields=fields, children=children)

    # struct也可以有子元素
    for child in elem:
        child_member = parse_element(child, enums, [])
        children.append(child_member)

    return StructDef(name=name, class_def=class_def)


def parse_element(elem, enums: List[EnumDef], structs: List[StructDef]) -> ChildMember:
    """解析一个XML元素为ChildMember。"""
    xml_name = elem.tag
    class_name = to_pascal_case(xml_name)

    container = elem.attrib.get('container_', '')
    key_field = elem.attrib.get('key_', '')
    struct_ref = elem.attrib.get('struct_', '')

    # 如果引用了预定义struct
    if struct_ref:
        struct_def = None
        for s in structs:
            if s.name == struct_ref:
                struct_def = s
                break
        if struct_def:
            # struct引用也支持set/unordered_map的key处理
            skey_cpp_type = ''
            skey_is_external = False
            if container in ('map', 'unordered_map') and key_field:
                # 先在struct字段中查找
                for f in struct_def.class_def.fields:
                    if f.xml_name == key_field:
                        skey_cpp_type = f.cpp_type
                        break
                # 若struct中没有，从元素自身属性中查找
                if not skey_cpp_type:
                    for attr_name, attr_type in elem.attrib.items():
                        if attr_name in DIRECTIVE_ATTRS:
                            continue
                        if attr_name == key_field:
                            f = parse_field(attr_name, attr_type, enums)
                            skey_cpp_type = f.cpp_type
                            skey_is_external = True
                            break
            if container == 'set' and key_field:
                for f in struct_def.class_def.fields:
                    if f.xml_name == key_field:
                        struct_def.class_def.set_key_member = f.member_name
                        break
            return ChildMember(
                xml_name=xml_name,
                member_name=xml_name,
                class_name=struct_ref,
                container=container,
                key_xml_name=key_field,
                key_cpp_type=skey_cpp_type,
                class_def=struct_def.class_def,
                is_struct_ref=True,
                key_is_external=skey_is_external,
            )

    # 解析简单字段
    fields = []
    for attr_name, attr_type in elem.attrib.items():
        if attr_name in DIRECTIVE_ATTRS:
            continue
        fields.append(parse_field(attr_name, attr_type, enums))

    # 创建类定义
    class_def = ClassDef(xml_name=xml_name, class_name=class_name, fields=fields)

    # 解析子元素
    for child in elem:
        child_member = parse_element(child, enums, structs)
        class_def.children.append(child_member)

    # 确定map/unordered_map的key类型
    key_cpp_type = ''
    if container in ('map', 'unordered_map') and key_field:
        for f in fields:
            if f.xml_name == key_field:
                key_cpp_type = f.cpp_type
                break

    # 如果用于set容器，记录key字段用于生成 operator<
    if container == 'set' and key_field:
        for f in fields:
            if f.xml_name == key_field:
                class_def.set_key_member = f.member_name
                break

    return ChildMember(
        xml_name=xml_name,
        member_name=xml_name,
        class_name=class_name,
        container=container,
        key_xml_name=key_field,
        key_cpp_type=key_cpp_type,
        class_def=class_def,
        is_struct_ref=False,
    )


def parse_template(xml_path: str) -> ParseResult:
    """解析XML模板文件。"""
    tree = ET.parse(xml_path)
    root = tree.getroot()

    root_class_name = to_pascal_case(root.tag)
    result = ParseResult()
    result.root_class = ClassDef(xml_name=root.tag, class_name=root_class_name)

    # 第一遍：提取enum和struct定义
    elements_to_parse = []
    for child in root:
        if child.tag == 'enum':
            result.enums.append(parse_enum(child))
        elif child.tag == 'struct':
            result.structs.append(parse_struct_def(child, result.enums))
        else:
            elements_to_parse.append(child)

    # 第二遍：解析常规元素
    for child in elements_to_parse:
        child_member = parse_element(child, result.enums, result.structs)
        result.root_class.children.append(child_member)

    return result


# ============================================================
# 头文件生成
# ============================================================

def collect_needed_includes(class_def: ClassDef, needed: Set[str]):
    """递归收集需要的include。"""
    for f in class_def.fields:
        if f.cpp_type == 'std::string':
            needed.add('string')
    for child in class_def.children:
        if child.container == 'vector':
            needed.add('vector')
        elif child.container == 'map':
            needed.add('map')
        elif child.container == 'set':
            needed.add('set')
        elif child.container == 'list':
            needed.add('list')
        elif child.container == 'unordered_map':
            needed.add('unordered_map')
        if child.class_def:
            collect_needed_includes(child.class_def, needed)


def gen_enum_header(enum: EnumDef, indent: int) -> List[str]:
    """生成枚举的头文件代码。"""
    pad = '    ' * indent
    lines = []
    lines.append(f'{pad}enum class {enum.name} : {enum.underlying_type}')
    lines.append(f'{pad}{{')
    for v_name, v_val in enum.values:
        lines.append(f'{pad}    {v_name} = {v_val},')
    lines.append(f'{pad}}};')
    return lines


def gen_class_header(class_def: ClassDef, indent: int, is_root: bool = False) -> List[str]:
    """递归生成类的头文件代码。"""
    pad = '    ' * indent
    cn = class_def.class_name
    lines = []

    lines.append(f'{pad}class {cn}')
    lines.append(f'{pad}{{')
    lines.append(f'{pad}public:')
    # 构造/析构/拷贝控制
    lines.append(f'{pad}    {cn}() = default;')
    lines.append(f'{pad}    ~{cn}() = default;')
    lines.append(f'{pad}    {cn}(const {cn}&) = delete;')
    lines.append(f'{pad}    {cn}& operator=(const {cn}&) = delete;')
    lines.append(f'{pad}    {cn}({cn}&&) = default;')
    lines.append(f'{pad}    {cn}& operator=({cn}&&) = default;')
    lines.append('')
    # 核心方法
    lines.append(f'{pad}    bool loadXml(const boost::property_tree::ptree& root);')
    if is_root:
        lines.append(f'{pad}    bool loadXml(const std::string& xml_file_path);')
    lines.append(f'{pad}    void dumpAll() const;')
    lines.append(f'{pad}    void clear();')
    lines.append('')

    # 路径相关 getter (仅根类)
    if is_root:
        lines.append(f'{pad}    const std::string& getXmlFilePath() const {{ return xml_file_path_; }}')
        lines.append('')

    # operator< (用于set容器)
    if class_def.set_key_member:
        lines.append(f'{pad}    bool operator<(const {cn}& other) const {{ return {class_def.set_key_member} < other.{class_def.set_key_member}; }}')
        lines.append('')

    # Getters
    for f in class_def.fields:
        getter_name = 'get' + to_pascal_case(f.xml_name)
        lines.append(f'{pad}    {f.cpp_type} {getter_name}() const {{ return {f.member_name}; }}')

    # 嵌套类 + 成员声明
    for child in class_def.children:
        lines.append('')
        if not child.is_struct_ref and child.class_def:
            lines.append(f'{pad}    // --- {child.class_name} ---')
            lines.extend(gen_class_header(child.class_def, indent + 1))
            lines.append('')

        if child.container == 'vector':
            lines.append(f'{pad}    std::vector<{child.class_name}> {child.member_name};')
        elif child.container == 'list':
            lines.append(f'{pad}    std::list<{child.class_name}> {child.member_name};')
        elif child.container == 'map':
            lines.append(f'{pad}    // <{child.key_xml_name}, {child.class_name}>')
            lines.append(f'{pad}    std::map<{child.key_cpp_type}, {child.class_name}> {child.member_name};')
        elif child.container == 'unordered_map':
            lines.append(f'{pad}    // <{child.key_xml_name}, {child.class_name}>')
            lines.append(f'{pad}    std::unordered_map<{child.key_cpp_type}, {child.class_name}> {child.member_name};')
        elif child.container == 'set':
            lines.append(f'{pad}    std::set<{child.class_name}> {child.member_name};')
        else:
            lines.append(f'{pad}    {child.class_name} {child.member_name};')

    # 私有字段
    lines.append('')
    lines.append(f'{pad}private:')
    for f in class_def.fields:
        lines.append(f'{pad}    {f.cpp_type} {f.member_name} = {f.default_val};')
    if is_root:
        lines.append(f'{pad}    std::string xml_file_path_ = "";')

    lines.append(f'{pad}}};')
    return lines


def gen_struct_header(struct_def: StructDef, indent: int) -> List[str]:
    """生成struct的头文件代码。"""
    pad = '    ' * indent
    cd = struct_def.class_def
    cn = cd.class_name
    lines = []

    lines.append(f'{pad}struct {cn}')
    lines.append(f'{pad}{{')
    lines.append(f'{pad}public:')
    # 构造/析构/拷贝控制
    lines.append(f'{pad}    {cn}() = default;')
    lines.append(f'{pad}    ~{cn}() = default;')
    lines.append(f'{pad}    {cn}(const {cn}&) = delete;')
    lines.append(f'{pad}    {cn}& operator=(const {cn}&) = delete;')
    lines.append(f'{pad}    {cn}({cn}&&) = default;')
    lines.append(f'{pad}    {cn}& operator=({cn}&&) = default;')
    lines.append('')
    # 核心方法
    lines.append(f'{pad}    bool loadXml(const boost::property_tree::ptree& root);')
    lines.append(f'{pad}    void dumpAll() const;')
    lines.append(f'{pad}    void clear();')
    lines.append('')

    # operator< (用于set容器)
    if cd.set_key_member:
        lines.append(f'{pad}    bool operator<(const {cn}& other) const {{ return {cd.set_key_member} < other.{cd.set_key_member}; }}')
        lines.append('')

    for f in cd.fields:
        getter_name = 'get' + to_pascal_case(f.xml_name)
        lines.append(f'{pad}    {f.cpp_type} {getter_name}() const {{ return {f.member_name}; }}')

    for child in cd.children:
        lines.append('')
        if not child.is_struct_ref and child.class_def:
            lines.append(f'{pad}    // --- {child.class_name} ---')
            lines.extend(gen_class_header(child.class_def, indent + 1))
            lines.append('')
        if child.container == 'vector':
            lines.append(f'{pad}    std::vector<{child.class_name}> {child.member_name};')
        elif child.container == 'list':
            lines.append(f'{pad}    std::list<{child.class_name}> {child.member_name};')
        elif child.container == 'map':
            lines.append(f'{pad}    // <{child.key_xml_name}, {child.class_name}>')
            lines.append(f'{pad}    std::map<{child.key_cpp_type}, {child.class_name}> {child.member_name};')
        elif child.container == 'unordered_map':
            lines.append(f'{pad}    // <{child.key_xml_name}, {child.class_name}>')
            lines.append(f'{pad}    std::unordered_map<{child.key_cpp_type}, {child.class_name}> {child.member_name};')
        elif child.container == 'set':
            lines.append(f'{pad}    std::set<{child.class_name}> {child.member_name};')
        else:
            lines.append(f'{pad}    {child.class_name} {child.member_name};')

    lines.append('')
    lines.append(f'{pad}private:')
    for f in cd.fields:
        lines.append(f'{pad}    {f.cpp_type} {f.member_name} = {f.default_val};')

    lines.append(f'{pad}}};')
    return lines


def generate_header(result: ParseResult, namespace: str, header_name: str) -> str:
    """生成完整的头文件内容。"""
    needed = set()  # 需要的STL头文件名

    for s in result.structs:
        collect_needed_includes(s.class_def, needed)
    collect_needed_includes(result.root_class, needed)

    for e in result.enums:
        if e.underlying_type == 'std::string':
            needed.add('string')

    lines = []
    lines.append('#pragma once')
    lines.append('')
    lines.append('#include <boost/property_tree/ptree.hpp>')
    lines.append('#include <cstdint>')
    lines.append('#include <iostream>')
    lines.append('#include <utility>')        # std::move
    lines.append('#include <stdexcept>')      # std::exception
    lines.append('#include <string>')         # std::string (xml_file_path_)
    if 'vector' in needed:
        lines.append('#include <string>')
    if 'vector' in needed:
        lines.append('#include <vector>')
    if 'map' in needed:
        lines.append('#include <map>')
    if 'set' in needed:
        lines.append('#include <set>')
    if 'list' in needed:
        lines.append('#include <list>')
    if 'unordered_map' in needed:
        lines.append('#include <unordered_map>')
    lines.append('')
    lines.append(f'namespace {namespace}')
    lines.append('{')
    lines.append('')

    if result.enums:
        lines.append('// ==================== Enums ====================')
        lines.append('')
        for e in result.enums:
            lines.extend(gen_enum_header(e, 1))
            lines.append('')

    if result.structs:
        lines.append('// ==================== Structs ====================')
        lines.append('')
        for s in result.structs:
            lines.extend(gen_struct_header(s, 1))
            lines.append('')

    lines.append('// ==================== Classes ====================')
    lines.append('')
    lines.extend(gen_class_header(result.root_class, 1, is_root=True))
    lines.append('')
    lines.append(f'}} // namespace {namespace}')
    lines.append('')

    return '\n'.join(lines)


# ============================================================
# CPP源文件生成
# ============================================================

def gen_loadxml_impl(class_def: ClassDef, qualified_name: str) -> List[str]:
    """生成 loadXml 方法实现。返回 bool，内部 try-catch。"""
    lines = []
    lines.append(f'bool {qualified_name}::loadXml(const boost::property_tree::ptree& root)')
    lines.append('{')
    lines.append('    try')
    lines.append('    {')

    # 加载简单字段（属性）
    for f in class_def.fields:
        if f.is_enum:
            utype = f.enum_underlying_type
            lines.append(f'        {f.member_name} = static_cast<{f.cpp_type}>(root.get<{utype}>("<xmlattr>.{f.xml_name}", 0));')
        elif f.cpp_type == 'bool':
            lines.append(f'        {f.member_name} = root.get<bool>("<xmlattr>.{f.xml_name}", false);')
        elif f.cpp_type == 'std::string':
            lines.append(f'        {f.member_name} = root.get<std::string>("<xmlattr>.{f.xml_name}", "");')
        else:
            lines.append(f'        {f.member_name} = root.get<{f.cpp_type}>("<xmlattr>.{f.xml_name}", {f.default_val});')

    # 加载子元素
    if class_def.children:
        if class_def.fields:
            lines.append('')
        lines.append('        for (const auto& child : root)')
        lines.append('        {')
        lines.append('            if (child.first == "<xmlattr>")')
        lines.append('                continue;')
        lines.append('')

        first = True
        for child in class_def.children:
            kw = 'if' if first else 'else if'
            first = False
            lines.append(f'            {kw} (child.first == "{child.xml_name}")')
            lines.append('            {')
            if child.container == '':
                lines.append(f'                if (!{child.member_name}.loadXml(child.second))')
                lines.append(f'                    return false;')
            elif child.container in ('vector', 'list'):
                lines.append(f'                {child.class_name} item;')
                lines.append(f'                if (!item.loadXml(child.second))')
                lines.append(f'                    return false;')
                lines.append(f'                {child.member_name}.push_back(std::move(item));')
            elif child.container in ('map', 'unordered_map'):
                lines.append(f'                {child.class_name} item;')
                lines.append(f'                if (!item.loadXml(child.second))')
                lines.append(f'                    return false;')
                if child.key_is_external:
                    key_default = TYPE_DEFAULTS.get(child.key_cpp_type, '0')
                    if child.key_cpp_type == 'bool':
                        key_default = 'false'
                    elif child.key_cpp_type == 'std::string':
                        key_default = '""'
                    lines.append(f'                auto keyVal = child.second.get<{child.key_cpp_type}>("<xmlattr>.{child.key_xml_name}", {key_default});')
                    lines.append(f'                {child.member_name}[keyVal] = std::move(item);')
                else:
                    key_getter = 'get' + to_pascal_case(child.key_xml_name) + '()'
                    lines.append(f'                {child.member_name}[item.{key_getter}] = std::move(item);')
            elif child.container == 'set':
                lines.append(f'                {child.class_name} item;')
                lines.append(f'                if (!item.loadXml(child.second))')
                lines.append(f'                    return false;')
                lines.append(f'                {child.member_name}.insert(std::move(item));')
            lines.append('            }')

        lines.append('        }')

    if not class_def.fields and not class_def.children:
        lines.append('        // no fields')

    lines.append('    }')
    lines.append(f'    catch (const std::exception& e)')
    lines.append('    {')
    lines.append(f'        std::cerr << "[{class_def.class_name}] loadXml failed: " << e.what() << std::endl;')
    lines.append(f'        return false;')
    lines.append('    }')
    lines.append('    return true;')
    lines.append('}')
    return lines


def gen_loadxml_file_impl(class_def: ClassDef, qualified_name: str) -> List[str]:
    """生成 loadXml(const std::string&) 重载：读取文件后调用 ptree 版本。"""
    lines = []
    lines.append(f'bool {qualified_name}::loadXml(const std::string& xml_file_path)')
    lines.append('{')
    lines.append('    xml_file_path_ = xml_file_path;')
    lines.append('    try')
    lines.append('    {')
    lines.append('        boost::property_tree::ptree tree;')
    lines.append('        boost::property_tree::read_xml(sMainConfig.config_dir.get() + xml_file_path_, tree);')
    lines.append(f'        return loadXml(tree.get_child("{class_def.class_name}"));')
    lines.append('    }')
    lines.append('    catch (const std::exception& e)')
    lines.append('    {')
    lines.append(f'        std::cerr << "[{class_def.class_name}] failed to read file \\"" << xml_file_path_ << "\\": " << e.what() << std::endl;')
    lines.append('        return false;')
    lines.append('    }')
    lines.append('}')
    return lines


def gen_dumpall_impl(class_def: ClassDef, qualified_name: str) -> List[str]:
    """生成 dumpAll 方法实现。"""
    lines = []
    lines.append(f'void {qualified_name}::dumpAll() const')
    lines.append('{')

    # 输出简单字段
    for f in class_def.fields:
        if f.is_enum:
            utype = f.enum_underlying_type
            lines.append(f'    std::cout << "  {f.xml_name}: " << static_cast<{utype}>({f.member_name}) << std::endl;')
        else:
            lines.append(f'    std::cout << "  {f.xml_name}: " << {f.member_name} << std::endl;')

    # 输出子元素
    for child in class_def.children:
        if class_def.fields:
            lines.append('')
        if child.container == '':
            lines.append(f'    std::cout << "[{child.xml_name}]" << std::endl;')
            lines.append(f'    {child.member_name}.dumpAll();')
        elif child.container in ('vector', 'list'):
            lines.append(f'    std::cout << "[{child.xml_name}] count=" << {child.member_name}.size() << std::endl;')
            lines.append(f'    for (const auto& item : {child.member_name})')
            lines.append('    {')
            lines.append(f'        item.dumpAll();')
            lines.append('    }')
        elif child.container in ('map', 'unordered_map'):
            lines.append(f'    std::cout << "[{child.xml_name}] count=" << {child.member_name}.size() << std::endl;')
            lines.append(f'    for (const auto& pair : {child.member_name})')
            lines.append('    {')
            lines.append(f'        pair.second.dumpAll();')
            lines.append('    }')
        elif child.container == 'set':
            lines.append(f'    std::cout << "[{child.xml_name}] count=" << {child.member_name}.size() << std::endl;')
            lines.append(f'    for (const auto& item : {child.member_name})')
            lines.append('    {')
            lines.append(f'        item.dumpAll();')
            lines.append('    }')

    if not class_def.fields and not class_def.children:
        lines.append('    // empty')

    lines.append('}')
    return lines


def gen_clear_impl(class_def: ClassDef, qualified_name: str) -> List[str]:
    """生成 clear 方法实现。"""
    lines = []
    lines.append(f'void {qualified_name}::clear()')
    lines.append('{')

    for f in class_def.fields:
        lines.append(f'    {f.member_name} = {f.default_val};')

    for child in class_def.children:
        if child.container == '':
            lines.append(f'    {child.member_name}.clear();')
        else:
            lines.append(f'    {child.member_name}.clear();')

    if not class_def.fields and not class_def.children:
        lines.append('    // nothing to clear')

    lines.append('}')
    return lines


def gen_class_cpp(class_def: ClassDef, scope: List[str], namespace: str, is_root: bool = False) -> List[str]:
    """递归生成类及其嵌套类的CPP实现。"""
    lines = []
    qualified = f'{namespace}::{"::".join(scope + [class_def.class_name])}'

    # 分隔注释
    full_path = '::'.join(scope + [class_def.class_name])
    lines.append(f'// ==================== {full_path} ====================')
    lines.append('')

    # loadXml (ptree)
    lines.extend(gen_loadxml_impl(class_def, qualified))
    lines.append('')

    # loadXml (file path) - 仅根类
    if is_root:
        lines.extend(gen_loadxml_file_impl(class_def, qualified))
        lines.append('')

    # dumpAll
    lines.extend(gen_dumpall_impl(class_def, qualified))
    lines.append('')

    # clear
    lines.extend(gen_clear_impl(class_def, qualified))
    lines.append('')

    # 递归生成嵌套类
    for child in class_def.children:
        if child.class_def and not child.is_struct_ref:
            lines.extend(gen_class_cpp(child.class_def, scope + [class_def.class_name], namespace))

    return lines


def gen_struct_cpp(struct_def: StructDef, namespace: str) -> List[str]:
    """生成struct的CPP实现。"""
    cd = struct_def.class_def
    qualified = f'{namespace}::{cd.class_name}'
    lines = []
    lines.append(f'// ==================== {cd.class_name} ====================')
    lines.append('')
    lines.extend(gen_loadxml_impl(cd, qualified))
    lines.append('')
    lines.extend(gen_dumpall_impl(cd, qualified))
    lines.append('')
    lines.extend(gen_clear_impl(cd, qualified))
    lines.append('')

    # struct的嵌套类
    for child in cd.children:
        if child.class_def and not child.is_struct_ref:
            lines.extend(gen_class_cpp(child.class_def, [cd.class_name], namespace))

    return lines


def generate_cpp(result: ParseResult, namespace: str, header_name: str) -> str:
    """生成完整的CPP源文件内容。"""
    lines = []
    lines.append(f'#include "{header_name}"')
    lines.append('#include <boost/property_tree/xml_parser.hpp>')
    lines.append(f'#include "config.h"')
    lines.append('')
    lines.append(f'namespace {namespace}')
    lines.append('{')
    lines.append('')

    # struct实现
    for s in result.structs:
        lines.extend(gen_struct_cpp(s, namespace))

    # 根类及嵌套类实现
    lines.extend(gen_class_cpp(result.root_class, [], namespace, is_root=True))

    lines.append(f'}} // namespace {namespace}')
    lines.append('')

    return '\n'.join(lines)


# ============================================================
# 主函数
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description='XML配置模板转C++代码生成工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
示例:
  python xml2cpp.py CampOfficialPromote.xml
  python xml2cpp.py CampOfficialPromote.xml --ns mygame --outdir ./src
  python xml2cpp.py CampOfficialPromote.xml --prefix CampConfig

支持的XML模板语法:
  1. 基本属性:         <tag attr1="uint32_t" attr2="std::string" />
  2. vector容器:       <tag attr="uint32_t" container_="vector" />
  3. list容器:         <tag attr="uint32_t" container_="list" />
  4. map容器:          <tag key="uint32_t" val="std::string" container_="map" key_="key" />
  5. unordered_map:    <tag key="uint32_t" val="std::string" container_="unordered_map" key_="key" />
  6. set容器:          <tag id="uint32_t" container_="set" key_="id" />
  7. 嵌套元素:         <parent attr="uint32_t"><child attr2="uint32_t" /></parent>
  8. enum定义:         <enum name="MyEnum" type="uint32_t"><value name="A" val="0" /></enum>
  9. enum引用:         <tag status="enum:MyEnum" />
  10. struct定义:      <struct name="Pos" x="uint32_t" y="uint32_t" />
  11. struct引用:      <tags struct_="Pos" container_="vector" />

生成的C++类特性:
  - 默认构造/析构函数
  - 禁止拷贝 (copy = delete)
  - 支持move语义 (move = default)
  - bool loadXml(ptree) 带异常处理，返回加载结果
  - bool loadXml(string) 便捷重载，传入文件路径直接读取
  - void dumpAll() 调试输出
  - void clear() 重置所有字段和容器
  - const string& getXmlFilePath() 获取最近加载的文件路径
  - 所有属性的 inline getter
  - set容器自动生成 operator<
'''
    )
    parser.add_argument('template', help='XML模板文件路径')
    parser.add_argument('--ns', default='xml', help='C++命名空间 (默认: xml)')
    parser.add_argument('--outdir', default='.', help='输出目录 (默认: 当前目录)')
    parser.add_argument('--prefix', default='', help='输出文件名前缀 (默认: 使用根元素名)')

    args = parser.parse_args()

    # 解析模板
    if not os.path.exists(args.template):
        print(f'错误: 文件不存在: {args.template}')
        sys.exit(1)

    result = parse_template(args.template)

    # 确定输出文件名
    if args.prefix:
        prefix = args.prefix
    else:
        prefix = result.root_class.class_name

    # 确保输出目录存在
    os.makedirs(args.outdir, exist_ok=True)

    header_name = f'{prefix}.h'
    cpp_name = f'{prefix}.cpp'

    header_path = os.path.join(args.outdir, header_name)
    cpp_path = os.path.join(args.outdir, cpp_name)

    # 生成代码
    header_content = generate_header(result, args.ns, header_name)
    cpp_content = generate_cpp(result, args.ns, header_name)

    # 写入文件
    with open(header_path, 'w', encoding='utf-8') as f:
        f.write(header_content)
    print(f'已生成头文件: {header_path}')

    with open(cpp_path, 'w', encoding='utf-8') as f:
        f.write(cpp_content)
    print(f'已生成源文件: {cpp_path}')

    # 打印统计信息
    enum_count = len(result.enums)
    struct_count = len(result.structs)

    def count_classes(cd):
        n = 1
        for child in cd.children:
            if child.class_def and not child.is_struct_ref:
                n += count_classes(child.class_def)
        return n

    class_count = count_classes(result.root_class)
    for s in result.structs:
        class_count += count_classes(s.class_def)

    print(f'\n统计: {class_count} 个类/struct, {enum_count} 个枚举, {struct_count} 个struct定义')


if __name__ == '__main__':
    main()