#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <windows.h>

// Simple JSON parser for our config
struct ConfigItem {
    std::wstring name;
    std::wstring path;
    std::wstring originalPath;
    std::string type;
};

struct ConfigFolder {
    std::wstring id;
    std::wstring name;
    std::string color;
    int posX;
    int posY;
    int iconSize = 64;
    int paneWidth = 360;
    int paneHeight = 280;
    std::vector<ConfigItem> items;
};

class Config {
public:
    static std::vector<ConfigFolder> LoadFolders(const std::wstring& path);
    static bool SaveFolders(const std::wstring& path, const std::vector<ConfigFolder>& folders);
    
private:
    static std::wstring Trim(const std::wstring& str);
    static std::string WStringToString(const std::wstring& wstr);
    static std::wstring StringToWString(const std::string& str);
    static std::string EscapeJsonString(const std::string& str);
    static std::string UnescapeJsonString(const std::string& str);
};

// Implementation
inline std::wstring Config::Trim(const std::wstring& str) {
    size_t first = str.find_first_not_of(L" \t\n\r\"");
    if (first == std::wstring::npos) return L"";
    size_t last = str.find_last_not_of(L" \t\n\r\"");
    return str.substr(first, last - first + 1);
}

inline std::string Config::WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
    return str;
}

inline std::wstring Config::StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}

// Escape backslashes and quotes for JSON
inline std::string Config::EscapeJsonString(const std::string& str) {
    std::string result;
    result.reserve(str.size() * 2);
    for (char c : str) {
        if (c == '\\') {
            result += "\\\\";
        } else if (c == '"') {
            result += "\\\"";
        } else {
            result += c;
        }
    }
    return result;
}

// Unescape JSON string
inline std::string Config::UnescapeJsonString(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            char next = str[i + 1];
            if (next == '\\') {
                result += '\\';
                i++;
            } else if (next == '"') {
                result += '"';
                i++;
            } else if (next == 'n') {
                result += '\n';
                i++;
            } else if (next == 't') {
                result += '\t';
                i++;
            } else {
                result += str[i];
            }
        } else {
            result += str[i];
        }
    }
    return result;
}

inline std::vector<ConfigFolder> Config::LoadFolders(const std::wstring& path) {
    std::vector<ConfigFolder> folders;
    
    std::ifstream file(path);
    if (!file.is_open()) {
        // Return default folder
        ConfigFolder def;
        def.id = L"folder-1";
        def.name = L"Oyunlar";
        def.color = "#3B82F6";
        def.posX = 100;
        def.posY = 100;
        def.iconSize = 64;
        def.paneWidth = 360;
        def.paneHeight = 280;
        folders.push_back(def);
        return folders;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    // Simple JSON parsing (handles our specific format)
    ConfigFolder current;
    bool inFolder = false;
    bool inItems = false;
    std::istringstream stream(content);
    std::string line;
    bool inItem = false;
    std::wstring itemName, itemPath, itemOriginalPath;
    std::string itemType;

    auto extractJsonField = [&](const std::string& source, const char* field, std::string& outValue) -> bool {
        std::string needle = std::string("\"") + field + "\"";
        size_t fieldPos = source.find(needle);
        if (fieldPos == std::string::npos) return false;

        size_t colonPos = source.find(":", fieldPos);
        if (colonPos == std::string::npos) return false;

        size_t quoteStart = source.find("\"", colonPos + 1);
        if (quoteStart == std::string::npos) return false;

        size_t quoteEnd = quoteStart + 1;
        while (quoteEnd < source.size()) {
            if (source[quoteEnd] == '"' && source[quoteEnd - 1] != '\\') {
                break;
            }
            quoteEnd++;
        }

        if (quoteEnd >= source.size()) return false;
        outValue = UnescapeJsonString(source.substr(quoteStart + 1, quoteEnd - quoteStart - 1));
        return true;
    };
    
    while (std::getline(stream, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        
        // Start of a folder object (but not an item object)
        if (line.find("{") != std::string::npos && !inFolder && !inItems) {
            inFolder = true;
            current = ConfigFolder();
            continue;
        }
        
        // End of a folder object
        if (line.find("}") != std::string::npos && inFolder && !inItems && !inItem) {
            folders.push_back(current);
            inFolder = false;
            continue;
        }
        
        // Start of items array
        if (line.find("\"items\"") != std::string::npos) {
            inItems = true;
            continue;
        }
        
        // End of items array
        if (line.find("]") != std::string::npos && inItems && !inItem) {
            inItems = false;
            continue;
        }
        
        // Inside items array - parse item objects
        if (inItems) {
            // Start of an item object
            if (line.find("{") != std::string::npos && !inItem) {
                inItem = true;
                itemName.clear();
                itemPath.clear();
                itemOriginalPath.clear();
                itemType.clear();
                continue;
            }
            
            // End of an item object
            if (line.find("}") != std::string::npos && inItem) {
                if (!itemName.empty() && !itemPath.empty()) {
                    current.items.push_back({itemName, itemPath, itemOriginalPath, itemType});
                }
                inItem = false;
                continue;
            }
            
            // Parse item fields (can be on same line like { "name": "...", "path": "..." })
            if (inItem || line.find("{") != std::string::npos) {
                std::string extracted;
                if (extractJsonField(line, "name", extracted)) {
                    itemName = StringToWString(extracted);
                }
                if (extractJsonField(line, "path", extracted)) {
                    itemPath = StringToWString(extracted);
                }
                if (extractJsonField(line, "originalPath", extracted)) {
                    itemOriginalPath = StringToWString(extracted);
                }
                if (extractJsonField(line, "type", extracted)) {
                    itemType = extracted;
                }
                
                // Single-line item format - { and } on same line
                if (line.find("{") != std::string::npos && line.find("}") != std::string::npos) {
                    if (!itemName.empty() && !itemPath.empty()) {
                        current.items.push_back({itemName, itemPath, itemOriginalPath, itemType});
                        itemName.clear();
                        itemPath.clear();
                        itemOriginalPath.clear();
                        itemType.clear();
                    }
                    inItem = false;
                } else if (line.find("{") != std::string::npos) {
                    inItem = true;
                }
            }
            continue;
        }
        
        // Parse key-value pairs for folder properties
        size_t colonPos = line.find(":");
        if (colonPos == std::string::npos) continue;
        
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);
        
        // Remove quotes and commas
        size_t quoteStart = key.find("\"");
        size_t quoteEnd = key.rfind("\"");
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
            key = key.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
        
        quoteStart = value.find("\"");
        quoteEnd = value.rfind("\"");
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
            value = value.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        } else {
            // Numeric value
            size_t numStart = value.find_first_of("0123456789-");
            size_t numEnd = value.find_last_of("0123456789");
            if (numStart != std::string::npos && numEnd != std::string::npos) {
                value = value.substr(numStart, numEnd - numStart + 1);
            }
        }
        
        if (key == "id") current.id = StringToWString(value);
        else if (key == "name") current.name = StringToWString(value);
        else if (key == "color") current.color = value;
        else if (key == "posX") current.posX = std::stoi(value);
        else if (key == "posY") current.posY = std::stoi(value);
        else if (key == "iconSize") current.iconSize = std::stoi(value);
        else if (key == "paneWidth") current.paneWidth = std::stoi(value);
        else if (key == "paneHeight") current.paneHeight = std::stoi(value);
        else if (key == "gridColumns") current.paneWidth = 24 + 84 * (std::stoi(value) <= 3 ? 3 : 4);
        else if (key == "visibleRows") current.paneHeight = 24 + 80 * (std::stoi(value) <= 3 ? 3 : (std::stoi(value) >= 5 ? 5 : 4));
    }
    
    if (folders.empty()) {
        ConfigFolder def;
        def.id = L"folder-1";
        def.name = L"Oyunlar";
        def.color = "#3B82F6";
        def.posX = 100;
        def.posY = 100;
        def.iconSize = 64;
        def.paneWidth = 360;
        def.paneHeight = 280;
        folders.push_back(def);
    }
    
    return folders;
}

inline bool Config::SaveFolders(const std::wstring& path, const std::vector<ConfigFolder>& folders) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    
    file << "[\n";
    for (size_t i = 0; i < folders.size(); i++) {
        const auto& f = folders[i];
        file << "    {\n";
        file << "        \"id\": \"" << WStringToString(f.id) << "\",\n";
        file << "        \"name\": \"" << WStringToString(f.name) << "\",\n";
        file << "        \"color\": \"" << f.color << "\",\n";
        file << "        \"posX\": " << f.posX << ",\n";
        file << "        \"posY\": " << f.posY << ",\n";
        file << "        \"iconSize\": " << f.iconSize << ",\n";
        file << "        \"paneWidth\": " << f.paneWidth << ",\n";
        file << "        \"paneHeight\": " << f.paneHeight << ",\n";
        file << "        \"items\": [\n";
        for (size_t j = 0; j < f.items.size(); j++) {
            std::string escapedName = EscapeJsonString(WStringToString(f.items[j].name));
            std::string escapedPath = EscapeJsonString(WStringToString(f.items[j].path));
            file << "            { \"name\": \"" << escapedName
                 << "\", \"path\": \"" << escapedPath << "\"";
            if (!f.items[j].originalPath.empty()) {
                file << ", \"originalPath\": \"" << EscapeJsonString(WStringToString(f.items[j].originalPath)) << "\"";
            }
            if (!f.items[j].type.empty()) {
                file << ", \"type\": \"" << EscapeJsonString(f.items[j].type) << "\"";
            }
            file << " }";
            if (j < f.items.size() - 1) file << ",";
            file << "\n";
        }
        file << "        ]\n";
        file << "    }";
        if (i < folders.size() - 1) file << ",";
        file << "\n";
    }
    file << "]\n";
    
    file.close();
    return true;
}
