#!/usr/bin/env python3
"""
Convert Sourcetrail XML project files (.srctrlprj) to TOML format.

Usage:
    python convert_xml_to_toml.py <input.srctrlprj> [output.toml]
    python convert_xml_to_toml.py --batch <directory>
"""

import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any, Dict, List, Optional

try:
    import tomli_w
except ImportError:
    print("Error: tomli_w not installed. Install with: pip install tomli_w")
    sys.exit(1)


def xml_to_dict(element: ET.Element) -> Any:
    """Convert XML element to Python dict/list structure."""
    
    # If element has no children and no attributes, return text
    if len(element) == 0 and len(element.attrib) == 0:
        text = element.text
        if text is None:
            return ""
        text = text.strip()
        
        # Try to convert to appropriate type
        if text.lower() == "true":
            return True
        elif text.lower() == "false":
            return False
        elif text.isdigit():
            return int(text)
        else:
            try:
                return float(text)
            except ValueError:
                return text
    
    # Build dict from children
    result: Dict[str, Any] = {}
    
    # Add attributes
    for key, value in element.attrib.items():
        result[f"@{key}"] = value
    
    # Process children
    for child in element:
        child_data = xml_to_dict(child)
        
        # Handle multiple children with same tag (convert to array)
        if child.tag in result:
            if not isinstance(result[child.tag], list):
                result[child.tag] = [result[child.tag]]
            result[child.tag].append(child_data)
        else:
            result[child.tag] = child_data
    
    return result


def convert_source_group(sg_element: ET.Element, sg_id: str) -> Dict[str, Any]:
    """Convert a source group XML element to TOML-friendly dict."""
    
    group: Dict[str, Any] = {"id": sg_id}
    
    for child in sg_element:
        tag = child.tag
        
        # Handle arrays (multiple elements with same tag)
        if tag in ["source_extension", "source_path", "exclude_filter", 
                   "indexed_header_path", "header_search_path", "framework_search_path",
                   "compiler_flag", "include_path", "class_path"]:
            # Convert to array
            array_key = tag + "s" if not tag.endswith("s") else tag
            if array_key not in group:
                group[array_key] = []
            
            text = child.text.strip() if child.text else ""
            if text:
                group[array_key].append(text)
        
        # Handle nested structures
        elif tag == "cross_compilation":
            group["cross_compilation"] = {}
            for cc_child in child:
                if cc_child.tag == "target":
                    group["cross_compilation"]["target"] = {}
                    for target_child in cc_child:
                        text = target_child.text.strip() if target_child.text else ""
                        group["cross_compilation"]["target"][target_child.tag] = text
                else:
                    text = cc_child.text.strip() if cc_child.text else ""
                    if text.lower() == "true":
                        group["cross_compilation"][cc_child.tag] = True
                    elif text.lower() == "false":
                        group["cross_compilation"][cc_child.tag] = False
                    else:
                        group["cross_compilation"][cc_child.tag] = text
        
        # Handle simple values
        else:
            text = child.text.strip() if child.text else ""
            
            # Convert boolean strings
            if text.lower() == "true":
                group[tag] = True
            elif text.lower() == "false":
                group[tag] = False
            elif text.isdigit():
                group[tag] = int(text)
            else:
                group[tag] = text
    
    return group


def convert_xml_to_toml(xml_path: Path) -> Dict[str, Any]:
    """Convert XML project file to TOML-compatible dict."""
    
    tree = ET.parse(xml_path)
    root = tree.getroot()
    
    toml_data: Dict[str, Any] = {}
    
    # Process top-level elements
    for child in root:
        if child.tag == "source_groups":
            # Handle source groups as array of tables
            toml_data["source_groups"] = []
            
            for sg in child:
                # Extract UUID from tag name (e.g., "source_group_UUID")
                sg_id = sg.tag.replace("source_group_", "")
                source_group = convert_source_group(sg, sg_id)
                toml_data["source_groups"].append(source_group)
        
        elif child.tag == "version":
            text = child.text.strip() if child.text else "8"
            toml_data["version"] = int(text)
        
        elif child.tag == "description":
            text = child.text.strip() if child.text else ""
            toml_data["description"] = text
        
        else:
            # Handle other top-level elements
            text = child.text.strip() if child.text else ""
            if text.lower() == "true":
                toml_data[child.tag] = True
            elif text.lower() == "false":
                toml_data[child.tag] = False
            elif text.isdigit():
                toml_data[child.tag] = int(text)
            else:
                toml_data[child.tag] = text
    
    return toml_data


def write_toml(data: Dict[str, Any], output_path: Path) -> None:
    """Write TOML data to file."""
    
    with open(output_path, "wb") as f:
        tomli_w.dump(data, f)
    
    print(f"✓ Converted to: {output_path}")


def convert_file(input_path: Path, output_path: Optional[Path] = None) -> bool:
    """Convert a single XML file to TOML."""
    
    if not input_path.exists():
        print(f"Error: File not found: {input_path}")
        return False
    
    if output_path is None:
        # Replace .srctrlprj with .srctrl.toml
        output_path = input_path.with_suffix(".srctrl.toml")
    
    try:
        print(f"Converting: {input_path}")
        toml_data = convert_xml_to_toml(input_path)
        write_toml(toml_data, output_path)
        return True
    
    except Exception as e:
        print(f"Error converting {input_path}: {e}")
        import traceback
        traceback.print_exc()
        return False


def convert_batch(directory: Path) -> None:
    """Convert all .srctrlprj files in directory."""
    
    xml_files = list(directory.rglob("*.srctrlprj"))
    
    if not xml_files:
        print(f"No .srctrlprj files found in {directory}")
        return
    
    print(f"Found {len(xml_files)} project files to convert\n")
    
    success_count = 0
    for xml_file in xml_files:
        if convert_file(xml_file):
            success_count += 1
        print()
    
    print(f"\nConversion complete: {success_count}/{len(xml_files)} successful")


def main():
    parser = argparse.ArgumentParser(
        description="Convert Sourcetrail XML project files to TOML format"
    )
    
    parser.add_argument(
        "input",
        type=Path,
        help="Input XML file or directory (with --batch)"
    )
    
    parser.add_argument(
        "output",
        type=Path,
        nargs="?",
        help="Output TOML file (optional, defaults to input.toml)"
    )
    
    parser.add_argument(
        "--batch",
        action="store_true",
        help="Convert all .srctrlprj files in directory"
    )
    
    args = parser.parse_args()
    
    if args.batch:
        convert_batch(args.input)
    else:
        convert_file(args.input, args.output)


if __name__ == "__main__":
    main()
