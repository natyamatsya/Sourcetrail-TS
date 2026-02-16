# Roadmap: Migrating Project Files from XML to TOML

## Executive Summary

This document outlines the incremental migration strategy for replacing XML-based `.srctrlprj` project files with TOML format. The migration will proceed in testable phases, allowing validation at each step without requiring backward compatibility.

## Current Architecture Analysis

### XML Implementation

**Core Components:**
- `ConfigManager` - Abstraction layer for configuration storage
- `Settings` - Base class for all settings (project, application, source groups)
- `ProjectSettings` - Main project configuration
- `SourceGroupSettings` - Per-language source group configurations

**Key Files:**
```
src/lib/utility/
└── ConfigManager.h/cpp          (XML read/write abstraction)

src/lib/settings/
├── Settings.h/cpp               (Base settings class)
├── ProjectSettings.h/cpp        (Project configuration)
└── source_group/
    ├── SourceGroupSettings*.h/cpp  (Language-specific settings)
```

**Current XML Structure:**
```xml
<?xml version="1.0" encoding="utf-8" ?>
<config>
    <version>8</version>
    <description>Project description</description>
    <source_groups>
        <source_group_[UUID]>
            <name>Source Group Name</name>
            <type>C++ Source Group</type>
            <status>enabled</status>
            <!-- Language-specific settings -->
        </source_group_[UUID]>
    </source_groups>
</config>
```

## Why TOML?

### Benefits
- ✅ **Human-readable** - Easy to edit manually
- ✅ **Modern standard** - Used by Rust (Cargo.toml), Python (pyproject.toml)
- ✅ **Type-safe** - Clear data types (strings, integers, arrays, tables)
- ✅ **Less verbose** - No closing tags, cleaner syntax
- ✅ **Better for version control** - Cleaner diffs
- ✅ **Excellent C++ support** - toml++ header-only library

### TOML Structure
```toml
version = 8
description = "Project description"

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

## Migration Strategy: Incremental Approach

### Phase 1: Infrastructure Setup (Week 1)

**Goal:** Add TOML support alongside XML without breaking existing functionality

**Tasks:**

1. **Add toml++ dependency**
   ```json
   // vcpkg.json
   "dependencies": [
     "tomlplusplus",
     // ... existing
   ]
   ```

2. **Create TOML ConfigManager implementation**
   ```
   src/lib/utility/
   ├── ConfigManager.h/cpp          (existing - abstract interface)
   ├── ConfigManagerXml.h/cpp       (new - extract XML logic)
   └── ConfigManagerToml.h/cpp      (new - TOML implementation)
   ```

3. **Implement format detection**
   ```cpp
   // ConfigManager.cpp
   static std::shared_ptr<ConfigManager> createAndLoad(
       std::shared_ptr<TextAccess> textAccess)
   {
       std::string content = textAccess->getText();
       
       if (content.find("<?xml") != std::string::npos) {
           return std::make_shared<ConfigManagerXml>(textAccess);
       } else {
           return std::make_shared<ConfigManagerToml>(textAccess);
       }
   }
   ```

**Deliverables:**
- toml++ integrated and compiling
- ConfigManagerToml skeleton class
- Format auto-detection working
- Unit tests for format detection

**Testing:**
- Load existing XML files (should still work)
- Detect XML vs TOML format correctly
- Basic TOML parsing test

---

### Phase 2: TOML Read Support (Week 2)

**Goal:** Implement reading TOML project files

**Tasks:**

1. **Implement ConfigManagerToml::getValue()**
   ```cpp
   // ConfigManagerToml.cpp
   bool ConfigManagerToml::getValue(const std::string& key, std::string& value) const
   {
       try {
           auto keys = splitKey(key); // "source_groups/id" -> ["source_groups", "id"]
           auto node = navigateToNode(m_root, keys);
           if (node.is_string()) {
               value = node.as_string()->get();
               return true;
           }
       } catch (...) {
           return false;
       }
       return false;
   }
   ```

2. **Implement array and table navigation**
   - Handle nested tables (e.g., `source_groups.cross_compilation.target`)
   - Handle array of tables (e.g., `[[source_groups]]`)
   - Map XML-style paths to TOML structure

3. **Key mapping strategy**
   ```cpp
   // Map XML hierarchical keys to TOML structure
   // XML: "source_groups/source_group_UUID/name"
   // TOML: source_groups[index].name
   ```

**Deliverables:**
- Complete read implementation
- All getValue/getValues methods working
- Unit tests for reading TOML

**Testing:**
- Create test TOML project files
- Load and verify all settings read correctly
- Compare with XML equivalents

---

### Phase 3: TOML Write Support (Week 3)

**Goal:** Implement writing TOML project files

**Tasks:**

1. **Implement ConfigManagerToml::setValue()**
   ```cpp
   bool ConfigManagerToml::setValue(const std::string& key, const std::string& value)
   {
       auto keys = splitKey(key);
       auto& node = ensureNodeExists(m_root, keys);
       node = value;
       return true;
   }
   ```

2. **Implement table and array creation**
   - Create nested tables as needed
   - Handle array of tables for source groups
   - Preserve order and formatting

3. **Implement save functionality**
   ```cpp
   bool ConfigManagerToml::save(const FilePath& filePath)
   {
       std::ofstream file(filePath.str());
       file << m_root;
       return file.good();
   }
   ```

**Deliverables:**
- Complete write implementation
- All setValue/setValues methods working
- Clean TOML output formatting

**Testing:**
- Create project programmatically
- Save to TOML
- Reload and verify integrity
- Check TOML formatting is clean

---

### Phase 4: Conversion Tool (Week 4)

**Goal:** Create tool to convert existing XML projects to TOML

**Tasks:**

1. **Create standalone conversion utility**
   ```cpp
   // tools/xml_to_toml_converter.cpp
   int main(int argc, char* argv[]) {
       FilePath xmlPath(argv[1]);
       FilePath tomlPath = xmlPath.withExtension(".toml");
       
       // Load XML
       auto xmlConfig = ConfigManagerXml::createAndLoad(xmlPath);
       
       // Create TOML
       auto tomlConfig = std::make_shared<ConfigManagerToml>();
       
       // Copy all values
       copyAllSettings(xmlConfig, tomlConfig);
       
       // Save TOML
       tomlConfig->save(tomlPath);
   }
   ```

2. **Implement batch conversion**
   - Find all `.srctrlprj` files in directory
   - Convert each to `.srctrlprj.toml`
   - Validate conversion
   - Generate report

3. **Add conversion to Sourcetrail UI**
   ```cpp
   // Add menu item: File -> Convert Project to TOML
   void QtMainWindow::convertProjectToToml() {
       if (confirmConversion()) {
           convertAndReload();
       }
   }
   ```

**Deliverables:**
- Command-line conversion tool
- UI conversion option
- Validation and error reporting

**Testing:**
- Convert all test projects
- Verify converted projects load correctly
- Compare XML and TOML behavior

---

### Phase 5: Update Project Creation (Week 5)

**Goal:** Make new projects use TOML by default

**Tasks:**

1. **Update ProjectSettings to use TOML**
   ```cpp
   // ProjectSettings.cpp
   bool ProjectSettings::save(const FilePath& filePath) {
       // Always save as TOML for new projects
       FilePath tomlPath = filePath.withExtension(".toml");
       m_config = std::make_shared<ConfigManagerToml>();
       // ... populate config
       return m_config->save(tomlPath);
   }
   ```

2. **Update project wizard**
   - Create `.srctrlprj.toml` instead of `.srctrlprj`
   - Update file dialogs to accept both extensions
   - Update documentation

3. **Update file extension handling**
   ```cpp
   const std::string PROJECT_FILE_EXTENSION = ".srctrlprj.toml";
   const std::string LEGACY_PROJECT_FILE_EXTENSION = ".srctrlprj";
   ```

**Deliverables:**
- New projects use TOML
- Project wizard updated
- File dialogs support both formats

**Testing:**
- Create new C++ project -> verify TOML
- Create new custom command project -> verify TOML
- Open old XML project -> still works
- Open new TOML project -> works

---

### Phase 6: Update Test Projects (Week 6)

**Goal:** Convert all test projects to TOML

**Tasks:**

1. **Convert testing directory projects**
   ```bash
   # Convert all test projects
   for file in testing/**/*.srctrlprj; do
       ./xml_to_toml_converter "$file"
       rm "$file"  # Remove XML version
       mv "${file}.toml" "$file"
   done
   ```

2. **Update test suite**
   - Update test expectations
   - Verify all tests pass with TOML
   - Update test data generation

3. **Update documentation**
   - Update README with TOML examples
   - Update project setup guide
   - Add migration guide

**Deliverables:**
- All test projects in TOML
- Test suite passing
- Documentation updated

**Testing:**
- Run full test suite
- Verify all test projects load
- Check UI with TOML projects

---

### Phase 7: Deprecate XML Support (Week 7-8)

**Goal:** Phase out XML format (optional - can keep for legacy support)

**Tasks:**

1. **Add deprecation warnings**
   ```cpp
   if (isXmlProject()) {
       showDeprecationWarning(
           "XML project format is deprecated. "
           "Please convert to TOML format."
       );
   }
   ```

2. **Auto-convert on load (optional)**
   ```cpp
   bool Project::load() {
       if (isXmlProject()) {
           if (autoConvertToToml()) {
               convertAndReload();
           }
       }
   }
   ```

3. **Remove XML support (optional - future)**
   - Remove ConfigManagerXml
   - Remove XML dependencies
   - Update file extension constants

**Deliverables:**
- Deprecation warnings in place
- Optional auto-conversion
- Migration complete

**Testing:**
- Load old XML project -> see warning
- Auto-convert works correctly
- All functionality preserved

---

## Implementation Details

### ConfigManager Interface

The existing `ConfigManager` interface needs to remain stable:

```cpp
class ConfigManager {
public:
    virtual ~ConfigManager() = default;
    
    // Read operations
    virtual bool getValue(const std::string& key, std::string& value) const = 0;
    virtual bool getValues(const std::string& key, std::vector<std::string>& values) const = 0;
    
    // Write operations
    virtual void setValue(const std::string& key, const std::string& value) = 0;
    virtual void setValues(const std::string& key, const std::vector<std::string>& values) = 0;
    
    // Persistence
    virtual bool save(const FilePath& filePath) = 0;
    
    // Factory methods
    static std::shared_ptr<ConfigManager> createEmpty();
    static std::shared_ptr<ConfigManager> createAndLoad(std::shared_ptr<TextAccess> textAccess);
};
```

### Key Mapping Strategy

**XML hierarchical paths → TOML structure:**

```cpp
// XML: "source_groups/source_group_UUID/name"
// TOML: source_groups[0].name (array of tables)

class KeyMapper {
    static std::vector<std::string> xmlKeyToTomlPath(const std::string& xmlKey) {
        // Handle special cases:
        // - source_group_UUID -> find index in array
        // - nested paths -> nested tables
        // - arrays -> TOML arrays
    }
};
```

### TOML Schema Design

**Project root:**
```toml
version = 8
description = "Project description"
```

**Source groups (array of tables):**
```toml
[[source_groups]]
id = "uuid"
name = "Name"
type = "C++ Source Group"
status = "enabled"

# Arrays
source_extensions = [".cpp", ".h"]
source_paths = ["src/"]
exclude_filters = ["*.tmp"]

# Nested tables
[source_groups.cross_compilation]
target_options_enabled = false

[source_groups.cross_compilation.target]
abi = "unknown"
arch = "x86_64"
```

### Error Handling

```cpp
class TomlConversionError : public std::runtime_error {
public:
    TomlConversionError(const std::string& msg) 
        : std::runtime_error("TOML conversion error: " + msg) {}
};

// Use in conversion
try {
    convertXmlToToml(xmlPath, tomlPath);
} catch (const TomlConversionError& e) {
    LOG_ERROR(e.what());
    showErrorDialog("Conversion failed: " + std::string(e.what()));
}
```

## Testing Strategy

### Unit Tests

```cpp
TEST_CASE("ConfigManagerToml reads simple values") {
    auto config = ConfigManagerToml::createFromString(R"(
        version = 8
        name = "Test Project"
    )");
    
    std::string name;
    REQUIRE(config->getValue("name", name));
    REQUIRE(name == "Test Project");
}

TEST_CASE("ConfigManagerToml reads nested tables") {
    auto config = ConfigManagerToml::createFromString(R"(
        [source_groups.target]
        arch = "x86_64"
    )");
    
    std::string arch;
    REQUIRE(config->getValue("source_groups/target/arch", arch));
    REQUIRE(arch == "x86_64");
}

TEST_CASE("ConfigManagerToml reads array of tables") {
    auto config = ConfigManagerToml::createFromString(R"(
        [[source_groups]]
        name = "Group 1"
        
        [[source_groups]]
        name = "Group 2"
    )");
    
    // Test array access
}
```

### Integration Tests

```cpp
TEST_CASE("Can create and save TOML project") {
    ProjectSettings settings;
    settings.setProjectFilePath("test", FilePath("/tmp/test.toml"));
    // ... configure project
    REQUIRE(settings.save());
    
    // Reload and verify
    ProjectSettings loaded;
    REQUIRE(loaded.load(FilePath("/tmp/test.toml")));
    REQUIRE(settings.equalsExceptNameAndLocation(loaded));
}

TEST_CASE("Can convert XML to TOML") {
    FilePath xmlPath = getTestProjectPath("test.srctrlprj");
    FilePath tomlPath = FilePath("/tmp/test.toml");
    
    convertXmlToToml(xmlPath, tomlPath);
    
    // Load both and compare
    ProjectSettings xmlSettings, tomlSettings;
    REQUIRE(xmlSettings.load(xmlPath));
    REQUIRE(tomlSettings.load(tomlPath));
    REQUIRE(xmlSettings.equalsExceptNameAndLocation(tomlSettings));
}
```

### Manual Testing Checklist

- [ ] Create new C++ project → saves as TOML
- [ ] Open TOML project → loads correctly
- [ ] Edit project settings → saves correctly
- [ ] Add/remove source groups → works
- [ ] Index project → database created correctly
- [ ] Convert XML project → conversion successful
- [ ] Open converted project → works identically
- [ ] All UI features work with TOML

## Migration Path for Users

### For Developers

1. **Pull latest code** with TOML support
2. **Rebuild** project
3. **Convert projects**: File → Convert Project to TOML
4. **Verify** project still works
5. **Commit** new `.toml` files

### For End Users

1. **Update Sourcetrail** to version with TOML support
2. **Open existing project** (XML still works)
3. **Optional**: Convert via menu or let auto-convert
4. **Continue using** as normal

## Risks & Mitigation

### Risk 1: Data Loss During Conversion
**Mitigation:** 
- Comprehensive validation after conversion
- Keep XML backup until verified
- Extensive testing with real projects

### Risk 2: TOML Library Issues
**Mitigation:**
- Use well-maintained toml++ library
- Extensive unit tests
- Fallback to XML if TOML fails

### Risk 3: Key Mapping Complexity
**Mitigation:**
- Clear mapping documentation
- Extensive testing of edge cases
- Validation tools

### Risk 4: User Confusion
**Mitigation:**
- Clear documentation
- Gradual rollout
- Support both formats during transition

## Success Metrics

- **Conversion accuracy:** 100% of settings preserved
- **Performance:** TOML load/save ≤ XML performance
- **Code quality:** Clean, maintainable implementation
- **User adoption:** Smooth transition, no complaints
- **Test coverage:** >90% for TOML code

## Timeline Summary

| Phase | Duration | Key Deliverable |
|-------|----------|-----------------|
| 1. Infrastructure | 1 week | TOML support added |
| 2. Read Support | 1 week | Can read TOML files |
| 3. Write Support | 1 week | Can write TOML files |
| 4. Conversion Tool | 1 week | XML→TOML converter |
| 5. New Projects | 1 week | New projects use TOML |
| 6. Test Projects | 1 week | All tests use TOML |
| 7. Deprecation | 2 weeks | XML phased out (optional) |
| **Total** | **8 weeks** | **Complete migration** |

## Next Steps

1. **Week 1:** Add tomlplusplus to vcpkg.json
2. **Week 1:** Create ConfigManagerToml skeleton
3. **Week 2:** Implement TOML reading
4. **Week 3:** Implement TOML writing
5. **Week 4:** Create conversion tool
6. **Week 5:** Switch new projects to TOML

## Code Examples

### Adding toml++ Dependency

```json
// vcpkg.json
{
  "dependencies": [
    "tomlplusplus",
    "boost-system",
    // ... rest
  ]
}
```

### Basic TOML Usage

```cpp
#include <toml++/toml.h>

// Parse TOML
auto config = toml::parse_file("project.toml");

// Read values
auto version = config["version"].value<int>();
auto name = config["source_groups"][0]["name"].value<std::string>();

// Write values
config.insert_or_assign("version", 9);

// Save
std::ofstream file("project.toml");
file << config;
```

### ConfigManagerToml Implementation Sketch

```cpp
class ConfigManagerToml : public ConfigManager {
public:
    ConfigManagerToml() : m_root(toml::table{}) {}
    
    bool getValue(const std::string& key, std::string& value) const override {
        auto node = navigateToNode(key);
        if (node && node->is_string()) {
            value = node->as_string()->get();
            return true;
        }
        return false;
    }
    
    void setValue(const std::string& key, const std::string& value) override {
        auto& node = ensureNodeExists(key);
        node = value;
    }
    
    bool save(const FilePath& filePath) override {
        std::ofstream file(filePath.str());
        file << m_root;
        return file.good();
    }
    
private:
    toml::table m_root;
    
    const toml::node* navigateToNode(const std::string& key) const;
    toml::node& ensureNodeExists(const std::string& key);
};
```

## References

- [toml++ Documentation](https://marzer.github.io/tomlplusplus/)
- [TOML Specification](https://toml.io/en/)
- [toml++ GitHub](https://github.com/marzer/tomlplusplus)

---

**Document Version:** 1.0  
**Last Updated:** 2026-02-16  
**Author:** Development Team  
**Status:** Ready for Implementation
