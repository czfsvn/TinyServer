#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
XML to C++ Code Generator
Usage: python xml2cpp.py <xml_file> [output_dir]
"""

import xml.etree.ElementTree as ET
import codecs
import sys
import os


def read_xml_with_encoding(file_path, encoding='UTF-8'):
    try:
        with codecs.open(file_path, 'r', encoding) as f:
            xml_content = f.read()
        root = ET.fromstring(xml_content)
        return root
    except Exception as e:
        print(f"Error reading XML file: {e}", file=sys.stderr)
        return None


def get_default_value(type_str):
    if 'uint32_t' in type_str:
        return '0'
    elif 'int32_t' in type_str:
        return '0'
    elif 'uint64_t' in type_str:
        return '0ULL'
    elif 'int64_t' in type_str:
        return '0LL'
    elif 'std::string' in type_str:
        return '""'
    elif 'float' in type_str:
        return '0.0f'
    elif 'double' in type_str:
        return '0.0'
    else:
        return '{}'


def to_class_name(name):
    if not name:
        return name
    return name[0].upper() + name[1:]


def to_member_name(name):
    return name.lower()


def generate_class_header(data, indent=0):
    result = []
    prefix = "    " * indent
    inner_prefix = "    " * (indent + 1)
    
    tag_name = to_class_name(data['tag'])
    member_name = to_member_name(data['tag'])
    
    result.append(f"{prefix}struct {tag_name}")
    result.append(f"{prefix}{{")
    
    container_type = data['attributes'].get('container_', '')
    key_attribute = data['attributes'].get('key_', '')
    
    result.append(f"{prefix}public:")
    result.append(f"{inner_prefix}void loadXml(const boost::property_tree::ptree& root);")
    result.append(f"{inner_prefix}void dumpAll() const;")
    
    normal_attrs = {}
    for attr_name, attr_value in data['attributes'].items():
        if not attr_name.startswith('container_') and not attr_name.startswith('key_'):
            normal_attrs[attr_name] = attr_value
    
    for attr_name, attr_value in normal_attrs.items():
        if attr_value == 'std::string':
            result.append(f"{inner_prefix}const {attr_value}& {attr_name}() const")
            result.append(f"{inner_prefix}{{")
            result.append(f"{inner_prefix}    return {attr_name}_;")
            result.append(f"{inner_prefix}}}")
        else:
            result.append(f"{inner_prefix}{attr_value} {attr_name}() const")
            result.append(f"{inner_prefix}{{")
            result.append(f"{inner_prefix}    return {attr_name}_;")
            result.append(f"{inner_prefix}}}")
    
    for child in data['children']:
        result.extend(generate_class_header(child, indent + 1))
    
    for child in data['children']:
        child_tag = to_class_name(child['tag'])
        child_member = to_member_name(child['tag'])
        child_container = child['attributes'].get('container_', '')
        child_key = child['attributes'].get('key_', '')
        
        if child_container == 'map' and child_key:
            key_type = child['attributes'].get(child_key, 'uint32_t')
            result.append(f"{inner_prefix}std::map<{key_type}, {child_tag}> {child_member};")
        elif child_container == 'vector':
            result.append(f"{inner_prefix}std::vector<{child_tag}> {child_member};")
    
    result.append(f"{prefix}private:")
    
    for attr_name, attr_value in normal_attrs.items():
        result.append(f"{inner_prefix}{attr_value} {attr_name}_ = {get_default_value(attr_value)};")
    
    for child in data['children']:
        child_tag = to_class_name(child['tag'])
        child_member = to_member_name(child['tag'])
        child_container = child['attributes'].get('container_', '')
        child_key = child['attributes'].get('key_', '')
        
        if not (child_container == 'map' and child_key) and child_container != 'vector':
            result.append(f"{inner_prefix}{child_tag} {child_member}_;")
    
    result.append(f"{prefix}}};")

    return result


def collect_member_instances(data, indent=2):
    result = []
    prefix = "    " * indent
    
    for child in data['children']:
        child_tag = to_class_name(child['tag'])
        child_member = to_member_name(child['tag'])
        child_container = child['attributes'].get('container_', '')
        child_key = child['attributes'].get('key_', '')
        
        if child_container != 'map' or not child_key:
            if child_container != 'vector':
                result.append(f"{prefix}{child_tag} {child_member};")
                result.append("")
    
    return result


def generate_load_xml(data, parent_class, indent=0):
    result = []
    prefix = "    " * indent
    inner_prefix = "    " * (indent + 1)
    
    tag_name = to_class_name(data['tag'])
    full_class = f"{parent_class}::{tag_name}" if parent_class else tag_name
    
    result.append(f"void {full_class}::loadXml(const boost::property_tree::ptree& root)")
    result.append(f"{prefix}{{")
    
    container_type = data['attributes'].get('container_', '')
    key_attribute = data['attributes'].get('key_', '')
    
    normal_attrs = {}
    for attr_name, attr_value in data['attributes'].items():
        if not attr_name.startswith('container_') and not attr_name.startswith('key_'):
            normal_attrs[attr_name] = attr_value
    
    for attr_name, attr_value in normal_attrs.items():
        result.append(f"{inner_prefix}{attr_name}_ = root.get_child(\"<xmlattr>\").get<{attr_value}>(\"{attr_name}\");")
    
    for child in data['children']:
        child_tag = to_class_name(child['tag'])
        child_member = to_member_name(child['tag'])
        child_container = child['attributes'].get('container_', '')
        child_key = child['attributes'].get('key_', '')
        
        if child_container == 'map' and child_key:
            result.append(f"{inner_prefix}{child_member}.clear();")
            result.append(f"{inner_prefix}auto range = root.equal_range(\"{child['tag']}\");")
            result.append(f"{inner_prefix}for (auto it = range.first; it != range.second; ++it)")
            result.append(f"{inner_prefix}{{")
            result.append(f"{inner_prefix}    {child_tag} item;")
            result.append(f"{inner_prefix}    item.loadXml(it->second);")
            result.append(f"{inner_prefix}    {child_member}[item.{child_key}] = item;")
            result.append(f"{inner_prefix}}}")
        elif child_container == 'vector':
            result.append(f"{inner_prefix}{child_member}.clear();")
            result.append(f"{inner_prefix}auto range = root.equal_range(\"{child['tag']}\");")
            result.append(f"{inner_prefix}for (auto it = range.first; it != range.second; ++it)")
            result.append(f"{inner_prefix}{{")
            result.append(f"{inner_prefix}    {child_tag} item;")
            result.append(f"{inner_prefix}    item.loadXml(it->second);")
            result.append(f"{inner_prefix}    {child_member}.push_back(item);")
            result.append(f"{inner_prefix}}}")
        else:
            result.append(f"{inner_prefix}{child_member}_.loadXml(root.get_child(\"{child['tag']}\"));")
    
    result.append(f"{prefix}}}")
    
    for child in data['children']:
        result.extend(generate_load_xml(child, full_class, indent))
    
    return result


def generate_dump_all(data, parent_class, indent=0):
    result = []
    prefix = "    " * indent
    inner_prefix = "    " * (indent + 1)
    
    tag_name = to_class_name(data['tag'])
    full_class = f"{parent_class}::{tag_name}" if parent_class else tag_name
    
    result.append(f"void {full_class}::dumpAll() const")
    result.append(f"{prefix}{{")
    
    normal_attrs = {}
    for attr_name, attr_value in data['attributes'].items():
        if not attr_name.startswith('container_') and not attr_name.startswith('key_'):
            normal_attrs[attr_name] = attr_value
    
    for attr_name in normal_attrs.keys():
        result.append(f'{inner_prefix}std::cout << "{attr_name}: " << {attr_name}_ << std::endl;')
    
    for child in data['children']:
        child_member = to_member_name(child['tag'])
        child_container = child['attributes'].get('container_', '')
        
        if child_container == 'map':
            result.append(f"{inner_prefix}for (auto& pair : {child_member})")
            result.append(f"{inner_prefix}{{")
            result.append(f"{inner_prefix}    std::cout << \"key: \" << pair.first << std::endl;")
            result.append(f"{inner_prefix}    pair.second.dumpAll();")
            result.append(f"{inner_prefix}}}")
        elif child_container == 'vector':
            result.append(f"{inner_prefix}for (auto& item : {child_member})")
            result.append(f"{inner_prefix}{{")
            result.append(f"{inner_prefix}    item.dumpAll();")
            result.append(f"{inner_prefix}}}")
        else:
            result.append(f"{inner_prefix}{child_member}_.dumpAll();")
    
    result.append(f"{prefix}}}")
    
    for child in data['children']:
        result.extend(generate_dump_all(child, full_class, indent))
    
    return result


def extract_data_structure(element):
    data = {
        'tag': element.tag,
        'attributes': dict(element.attrib),
        'children': []
    }
    
    for child in element:
        child_data = extract_data_structure(child)
        data['children'].append(child_data)
    
    return data


def generate_header_file(data_structure, output_dir):
    root_tag = to_class_name(data_structure['tag'])
    header_file = os.path.join(output_dir, f"{root_tag.lower()}_data.h")
    
    with open(header_file, 'w') as f:
        f.write(f"#ifndef __{root_tag.upper()}_DATA_H__\n")
        f.write(f"#define __{root_tag.upper()}_DATA_H__\n")
        f.write("\n")
        f.write("#include <map>\n")
        f.write("#include <string>\n")
        f.write("#include <vector>\n")
        f.write("#include <boost/property_tree/ptree.hpp>\n")
        f.write("\n")
        f.write("namespace xmldata\n")
        f.write("{\n")
        f.write("\n")
        f.write(f"    class {root_tag}Data\n")
        f.write("    {\n")
        
        f.write("    public:\n")
        f.write("        void loadXml(const std::string& xml_file_path);\n")
        f.write("        void dumpAll() const;\n")
        f.write("        std::string& getFileName()\n")
        f.write("        {\n")
        f.write("            return xml_file_path_;\n")
        f.write("        }\n")
        f.write("\n")
        
        for child in data_structure['children']:
            lines = generate_class_header(child, 2)
            for line in lines:
                f.write(line + "\n")
        
        f.write("    public:\n")
        instances = collect_member_instances(data_structure)
        for inst in instances:
            f.write(inst + "\n")
        
        f.write("    private:\n")
        f.write("        std::string xml_file_path_;\n")
        f.write("    };\n")
        f.write("\n")
        f.write("} // namespace xmldata\n")
        f.write("\n")
        f.write(f"#endif // __{root_tag.upper()}_DATA_H__\n")
    
    return header_file


def generate_source_file(data_structure, output_dir):
    root_tag = to_class_name(data_structure['tag'])
    source_file = os.path.join(output_dir, f"{root_tag.lower()}_data.cpp")
    
    with open(source_file, 'w') as f:
        header_name = f"{root_tag.lower()}_data.h"
        f.write(f'#include "{header_name}"\n')
        f.write("#include <boost/property_tree/xml_parser.hpp>\n")
        f.write("#include <iostream>\n")
        f.write("\n")
        f.write("namespace xmldata\n")
        f.write("{\n")
        f.write("\n")
        f.write("typedef boost::property_tree::ptree ptree;\n")
        f.write("\n")
        
        for child in data_structure['children']:
            lines = generate_load_xml(child, f"{root_tag}Data", 0)
            for line in lines:
                f.write(line + "\n")
            f.write("\n")
            
            lines = generate_dump_all(child, f"{root_tag}Data", 0)
            for line in lines:
                f.write(line + "\n")
            f.write("\n")
        
        f.write(f"void {root_tag}Data::loadXml(const std::string& xml_file_path)\n")
        f.write("{\n")
        f.write("    xml_file_path_ = xml_file_path;\n")
        f.write("    ptree tree;\n")
        f.write("    boost::property_tree::read_xml(xml_file_path_, tree);\n")
        
        for child in data_structure['children']:
            child_member = to_member_name(child['tag'])
            f.write(f"    {child_member}.loadXml(tree.get_child(\"{child['tag']}\"));\n")
        
        f.write("}\n")
        f.write("\n")
        
        f.write(f"void {root_tag}Data::dumpAll() const\n")
        f.write("{\n")
        for child in data_structure['children']:
            child_member = to_member_name(child['tag'])
            f.write(f"    {child_member}.dumpAll();\n")
        f.write("}\n")
        f.write("\n")
        f.write("} // namespace xmldata\n")
    
    return source_file


def main():
    if len(sys.argv) < 2:
        print("Usage: python xml2cpp.py <xml_file> [output_dir]", file=sys.stderr)
        sys.exit(1)
    
    xml_file = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(xml_file)
    
    if not os.path.exists(xml_file):
        print(f"Error: XML file not found - {xml_file}", file=sys.stderr)
        sys.exit(1)
    
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    root = read_xml_with_encoding(xml_file)
    if root is None:
        sys.exit(1)
    
    data_structure = extract_data_structure(root)
    
    header_file = generate_header_file(data_structure, output_dir)
    source_file = generate_source_file(data_structure, output_dir)
    
    print(f"Generated: {header_file}")
    print(f"Generated: {source_file}")


if __name__ == '__main__':
    main()