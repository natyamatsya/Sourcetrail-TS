# Sourcetrail Tools

## XML to TOML Converter

Convert Sourcetrail XML project files (`.srctrlprj`) to TOML format.

### Installation

```bash
pip install -r requirements.txt
```

### Usage

**Convert a single file:**

```bash
python convert_xml_to_toml.py project.srctrlprj
# Creates: project.srctrl.toml

# Or specify output:
python convert_xml_to_toml.py project.srctrlprj output.srctrl.toml
```

**Convert all files in a directory:**

```bash
python convert_xml_to_toml.py --batch testing/
# Converts all .srctrlprj files found recursively
```

**Make executable (Unix/Mac):**

```bash
chmod +x convert_xml_to_toml.py
./convert_xml_to_toml.py project.srctrlprj
```

### Examples

Convert test projects:

```bash
python convert_xml_to_toml.py --batch testing/
```

Convert a specific project:

```bash
python convert_xml_to_toml.py \
    testing/graph_view/graph_view_tests.srctrlprj \
    testing/graph_view/graph_view_tests.srctrl.toml
```

### Output Format

The converter creates clean, readable TOML files:

```toml
version = 8
description = "Test project"

[[source_groups]]
id = "475571d1-b082-4fff-81c5-d099c19f957d"
name = "C++ Source Group"
type = "C++ Source Group"
status = "enabled"
cpp_standard = "c++17"
source_extensions = [".cpp", ".cxx", ".cc"]
source_paths = ["data"]
exclude_filters = ["data/files/template.h"]

[source_groups.cross_compilation.target]
abi = "unknown"
arch = "x86_64"
sys = "unknown"
vendor = "unknown"
```

### Validation

After conversion, you can validate the TOML file:

```bash
python -c "import tomllib; tomllib.load(open('project.srctrl.toml', 'rb'))"
```

(Python 3.11+ has `tomllib` built-in, otherwise use `tomli`)
