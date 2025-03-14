#include <iostream>
#include "fs.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <iomanip>


FS::FS(){
    current_directory_block = ROOT_BLOCK;
}

FS::~FS()
{}

int FS::format(){
    std::memset(fat, FAT_FREE, sizeof(fat));
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;

    dir_entry root_dir[BLOCK_SIZE / sizeof(dir_entry)];
    std::memset(root_dir, 0, sizeof(root_dir));
    if (disk.write(ROOT_BLOCK, (uint8_t *)root_dir) != 0)
        return -1;

    if (disk.write(FAT_BLOCK, (uint8_t *)fat) != 0)
        return -1;

    return 0;
}

int FS::create(std::string filepath) {
    if (filepath.length() > 55) {
        std::cout << "File name too long.\n";
        return -1;
    }

    uint16_t parent_block;
    std::string filename;

    size_t last_slash = filepath.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string parent_path = filepath.substr(0, last_slash);
        filename = filepath.substr(last_slash + 1);

        if (filename.empty()) {
            std::cout << "No file name. \n";
            return -1;
        }

        if (resolvePath(parent_path.empty() ? "/" : parent_path, parent_block) != 0) {
            std::cout << "Parent directory not found. \n";
            return -1;
        }
    } else {
        parent_block = current_directory_block;
        filename = filepath;
    }

    dir_entry parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(parent_block, reinterpret_cast<uint8_t*>(parent_dir));

    for (const auto& entry : parent_dir) {
        if (std::strcmp(entry.file_name, filename.c_str()) == 0) {
            std::cout << "File already exists. \n";
            return -1;
        }
    }

    std::vector<uint8_t> file_data;
    std::string input_line;
    std::cout << "Enter file content: \n";
    while (std::getline(std::cin, input_line) && !input_line.empty()) {
        file_data.insert(file_data.end(), input_line.begin(), input_line.end());
        file_data.push_back('\n');
    }

    uint16_t first_block = allocateBlocks(file_data);
    if (first_block == FAT_FREE) {
        std::cout << "Disk full.\n";
        return -1;
    }

    dir_entry new_file;
    std::memset(&new_file, 0, sizeof(new_file));
    std::strncpy(new_file.file_name, filename.c_str(), sizeof(new_file.file_name) - 1);
    new_file.size = file_data.size();
    new_file.first_blk = first_block;
    new_file.type = TYPE_FILE;
    new_file.access_rights = READ | WRITE | EXECUTE; 

    if (!addToDirectory(parent_block, new_file))
        return -1;
    
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    return 0;
}

int FS::cat(std::string filepath) {
    uint16_t file_block;
    dir_entry file_entry;

    if (resolvefilePath(filepath, file_block, file_entry) != 0) {
        std::cout << "File not found. \n";
        return -1;
    }

    if (file_entry.type != TYPE_FILE) {
        std::cout << "Target is not a file. \n";
        return -1;
    }

    if ((file_entry.access_rights & READ) == 0) {
        std::cout << "You do not have read access for this file. \n";
        return -1;
    }

    uint16_t block = file_entry.first_blk;
    int remaining_size = file_entry.size;
    uint8_t buffer[BLOCK_SIZE];

    while (block != FAT_EOF && remaining_size > 0) {
        disk.read(block, buffer);
        int bytes_to_read = std::min(remaining_size, BLOCK_SIZE);
        std::cout.write(reinterpret_cast<char*>(buffer), bytes_to_read);
        remaining_size -= bytes_to_read;
        block = fat[block];
    }
    
    return 0;
}

int FS::ls() {
    dir_entry current_dir[BLOCK_SIZE / sizeof(dir_entry)];
    std::memset(current_dir, 0, sizeof(current_dir));
    disk.read(current_directory_block, reinterpret_cast<uint8_t*>(current_dir));

    std::cout << "name\ttype\taccessrights\tsize\n";

    for (const auto& entry : current_dir) {
        if (entry.file_name[0] != '\0' && 
            std::strcmp(entry.file_name, ".") != 0 && 
            std::strcmp(entry.file_name, "..") != 0 && 
            (entry.type == TYPE_FILE || entry.type == TYPE_DIR)) {

            std::string accessrights = "---";
            if (entry.access_rights & READ) accessrights[0] = 'r';
            if (entry.access_rights & WRITE) accessrights[1] = 'w';
            if (entry.access_rights & EXECUTE) accessrights[2] = 'x';

            std::cout << entry.file_name << "\t" 
                      << (entry.type == TYPE_DIR ? "dir" : "file") << "\t" 
                      << accessrights << "\t\t" 
                      << (entry.type == TYPE_DIR ? "-" : std::to_string(entry.size)) << "\n";
        }
    }
    return 0;
}

int FS::cp(std::string sourcepath, std::string destpath) {
    uint16_t source_block, dest_dir_block;
    dir_entry source_file;

    if (resolvefilePath(sourcepath, source_block, source_file) != 0) {
        std::cout << "Source file/path not found. \n";
        return -1;
    }

    if (destpath == "..") {
        if (current_path.empty()) {
            std::cout << "You are at the root. \n";
            return -1;
        }
        std::string parent_path = pwdToString();
        size_t last_slash = parent_path.find_last_of('/');

        if (last_slash != std::string::npos)
            parent_path = parent_path.substr(0, last_slash);

        if (resolvePath(parent_path, dest_dir_block) != 0) {
            std::cout << "Could not find dir path. \n";
            return -1;
        }
        destpath = source_file.file_name;

    } else {
        if (resolvePath(destpath, dest_dir_block) != 0) {
            size_t last_slash = destpath.find_last_of('/');
            std::string dir_path = (last_slash == std::string::npos) ? "." : destpath.substr(0, last_slash);
            std::string file_name = (last_slash == std::string::npos) ? destpath : destpath.substr(last_slash + 1);

            if (resolvePath(dir_path, dest_dir_block) != 0) {
                std::cout << "Destination dir not found. \n";
                return -1;
            }
            destpath = file_name;
        } else {
            destpath = source_file.file_name;
        }
    }

    dir_entry entries[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(dest_dir_block, reinterpret_cast<uint8_t*>(entries));

    for (const auto& entry : entries) {
        if (std::strcmp(entry.file_name, destpath.c_str()) == 0) {
            std::cout << "File with that name already exists. \n";
            return -1;
        }
    }

    std::vector<uint8_t> file_data;
    uint16_t block = source_file.first_blk;
    int remaining_size = source_file.size;
    uint8_t buffer[BLOCK_SIZE];

    while (block != FAT_EOF && remaining_size > 0) {
        disk.read(block, buffer);
        int bytes_to_read = std::min(remaining_size, BLOCK_SIZE);
        file_data.insert(file_data.end(), buffer, buffer + bytes_to_read);
        remaining_size -= bytes_to_read;
        block = fat[block];
    }

    int file_size = file_data.size();
    uint16_t first_block = allocateBlocks(file_data);

    if (first_block == FAT_FREE) {
        std::cout << "No space for file. \n";
        return -1;
    }

    dir_entry new_file = source_file;
    std::strncpy(new_file.file_name, destpath.c_str(), sizeof(new_file.file_name));
    new_file.first_blk = first_block;
    new_file.size = file_size;

    if (!addToDirectory(dest_dir_block, new_file)) {
        std::cout << "Could not add file. \n";
        return -1;
    }

    return 0;
}

int FS::mv(std::string sourcepath, std::string destpath) {
    uint16_t source_file_block, source_dir_block, dest_dir_block;
    dir_entry source_file;

    if (resolvefilePath(sourcepath, source_file_block, source_file) != 0) {
        std::cout << "Source file/path not found. \n";
        return -1;
    }

    size_t last_slash = sourcepath.find_last_of('/');
    std::string source_dir_path = (last_slash == std::string::npos) ? "." : sourcepath.substr(0, last_slash);

    if (resolvePath(source_dir_path, source_dir_block) != 0) {
        std::cout << "Could not find source parent dir. \n";
        return -1;
    }

    if (destpath == "..") {
        if (current_path.empty()) {
            std::cout << "You are at the root. \n";
            return -1;
        }

        std::string parent_path = pwdToString();
        size_t last_slash = parent_path.find_last_of('/');
        parent_path = (last_slash == 0) ? "/" : parent_path.substr(0, last_slash);

        if (resolvePath(parent_path, dest_dir_block) != 0)
            return -1;
        

        destpath = source_file.file_name;
    } else if (destpath == "/") {
        dest_dir_block = ROOT_BLOCK;
        destpath = source_file.file_name;
    } else {
        if (resolvePath(destpath, dest_dir_block) != 0) {
            size_t last_slash = destpath.find_last_of('/');
            std::string dir_path = (last_slash == std::string::npos) ? "." : destpath.substr(0, last_slash);
            std::string file_name = (last_slash == std::string::npos) ? destpath : destpath.substr(last_slash + 1);

            if (resolvePath(dir_path, dest_dir_block) != 0) {
                std::cout << "Destination dir not found. \n";
                return -1;
            }

            destpath = file_name; 
        } else {
            destpath = source_file.file_name;
        }
    }

    dir_entry entries[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(dest_dir_block, reinterpret_cast<uint8_t*>(entries));

    for (const auto& entry : entries) {
        if (std::strcmp(entry.file_name, destpath.c_str()) == 0) {
            std::cout << "File already exists at destination with that name. \n";
            return -1;
        }
    }

    dir_entry new_file = source_file;
    std::strncpy(new_file.file_name, destpath.c_str(), sizeof(new_file.file_name));

    if (!addToDirectory(dest_dir_block, new_file)) {
        std::cout << "Failed to add file. \n";
        return -1;
    }

    if (!removeFromDirectory(source_dir_block, source_file.file_name))
        return -1;
    
    return 0;
}

int FS::rm(std::string filepath) {
    uint16_t file_block;
    dir_entry file_entry;

    if (filepath.find('/') == std::string::npos) 
        filepath = "./" + filepath;
    
    if (resolvefilePath(filepath, file_block, file_entry) != 0) {
        std::cout << "File/ path not found. \n";
        return -1;
    }

    if (file_entry.type == TYPE_DIR) {
        dir_entry dir_content[BLOCK_SIZE / sizeof(dir_entry)];
        disk.read(file_entry.first_blk, reinterpret_cast<uint8_t*>(dir_content));

        for (const auto& entry : dir_content) {
            if (entry.file_name[0] != '\0' && std::strcmp(entry.file_name, "..") != 0) {
                std::cout << "Directory is not empty, cannot remove.\n";
                return -1;
            }
        }
    }

    int16_t block = file_entry.first_blk;
    while (block != FAT_EOF) {
        int16_t next_block = fat[block];
        fat[block] = FAT_FREE;
        block = next_block;
    }

    uint16_t parent_dir_block;
    std::string parent_dir_path = filepath.substr(0, filepath.find_last_of('/'));

    if (parent_dir_path.empty() || parent_dir_path == ".") {
        parent_dir_block = current_directory_block;
    } else if (resolvePath(parent_dir_path, parent_dir_block) != 0) {
        std::cout << "Parent directory not found.\n";
        return -1;
    }

    dir_entry parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(parent_dir_block, reinterpret_cast<uint8_t*>(parent_dir));

    for (auto& entry : parent_dir) {
        if (std::strcmp(entry.file_name, file_entry.file_name) == 0) {
            std::memset(&entry, 0, sizeof(entry));
            break;
        }
    }

    disk.write(parent_dir_block, reinterpret_cast<uint8_t*>(parent_dir));
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    return 0;
}

int FS::append(std::string filepath1, std::string filepath2) {
    dir_entry current_dir[BLOCK_SIZE / sizeof(dir_entry)];
    std::memset(current_dir, 0, sizeof(current_dir));
    disk.read(current_directory_block, reinterpret_cast<uint8_t*>(current_dir));

    dir_entry* file1 = nullptr;
    dir_entry* file2 = nullptr;

    for (auto& entry : current_dir) {
        if (std::strcmp(entry.file_name, filepath1.c_str()) == 0 && entry.type == TYPE_FILE)
            file1 = &entry;
        if (std::strcmp(entry.file_name, filepath2.c_str()) == 0 && entry.type == TYPE_FILE)
            file2 = &entry;
    }

    if (!file1 || !file2) {
        std::cout << "One or both files not found.\n";
        return -1;
    }

    if ((file1->access_rights & 0b100) == 0) { 
        std::cout << "No read access for file1. \n";
        return -1;
    }

    if ((file2->access_rights & 0b010) == 0) {
        std::cout << "No read access for file2. \n";
        return -1;
    }

    std::vector<uint8_t> file1_data;
    uint16_t block = file1->first_blk;
    int remaining_size = file1->size;
    uint8_t buffer[BLOCK_SIZE];

    while (block != FAT_EOF && remaining_size > 0) {
        disk.read(block, buffer);

        int bytes_to_read = std::min(remaining_size, BLOCK_SIZE);
        file1_data.insert(file1_data.end(), buffer, buffer + bytes_to_read);

        remaining_size -= bytes_to_read;
        block = fat[block];
    }

    int new_size = file2->size + file1_data.size();
    int remaining = file1_data.size();
    int16_t last_block = file2->first_blk;
    int16_t prev_block = -1;

    while (fat[last_block] != FAT_EOF) {
        prev_block = last_block;
        last_block = fat[last_block];
    }

    int block_offset = file2->size % BLOCK_SIZE;
    if (block_offset > 0) {
        uint8_t last_block_data[BLOCK_SIZE];
        disk.read(last_block, last_block_data);
        int space_in_last_block = BLOCK_SIZE - block_offset;
        int bytes_to_append = std::min(remaining, space_in_last_block);
        std::memcpy(last_block_data + block_offset, file1_data.data(), bytes_to_append);
        disk.write(last_block, last_block_data);
        remaining -= bytes_to_append;
        if (remaining == 0) {
            file2->size = new_size;
            disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t*>(current_dir));
            disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
            return 0;
        }
    }

    for (int i = 0; remaining > 0; ++i) {
        int16_t free_block = -1;
        for (int blk = 2; blk < BLOCK_SIZE / 2; ++blk) {
            if (fat[blk] == FAT_FREE) {
                free_block = blk;
                fat[blk] = FAT_EOF;
                break;
            }
        }

        if (free_block == -1) {
            std::cout << "No free space available. \n";
            return -1;
        }

        if (prev_block != -1) fat[prev_block] = free_block;
        prev_block = free_block;

        int bytes_to_append = std::min(remaining, BLOCK_SIZE);
        uint8_t block_data[BLOCK_SIZE] = {0};
        std::memcpy(block_data, file1_data.data() + i * BLOCK_SIZE, bytes_to_append);
        disk.write(free_block, block_data);
        remaining -= bytes_to_append;
    }

    file2->size = new_size;
    disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t*>(current_dir));
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    return 0;
}

int FS::mkdir(std::string dirpath) {
    uint16_t parent_block;
    std::string parent_path, new_dir_name;

    if (dirpath.find('/') == std::string::npos) {
        parent_path = ".";
        new_dir_name = dirpath;
    } else if (dirpath[0] == '/') {
        size_t last_slash = dirpath.find_last_of('/');
        parent_path = (last_slash == 0) ? "/" : dirpath.substr(0, last_slash);
        new_dir_name = dirpath.substr(last_slash + 1);
    } else {
        size_t last_slash = dirpath.find_last_of('/');
        parent_path = dirpath.substr(0, last_slash);
        new_dir_name = dirpath.substr(last_slash + 1);
    }

    if (new_dir_name.empty()) {
        std::cout << "Invalid directory name.\n";
        return -1;
    }

    if (resolvePath(parent_path.empty() ? "." : parent_path, parent_block) != 0) {
        std::cout << "Cant handle path. \n";
        return -1;
    }

    dir_entry parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(parent_block, reinterpret_cast<uint8_t*>(parent_dir));

    for (const auto& entry : parent_dir) {
        if (strcmp(entry.file_name, new_dir_name.c_str()) == 0) {
            std::cout << "File or dir named that already exists. \n";
            return -1;
        }
    }

    int16_t free_block = -1;
    for (int blk = 2; blk < BLOCK_SIZE / 2; ++blk) {
        if (fat[blk] == FAT_FREE) {
            free_block = blk;
            fat[blk] = FAT_EOF;
            break;
        }
    }

    if (free_block == -1) {
        std::cout << "No free blocks available to create directory. \n";
        return -1;
    }

    dir_entry new_dir;
    std::memset(&new_dir, 0, sizeof(new_dir));
    std::strncpy(new_dir.file_name, new_dir_name.c_str(), sizeof(new_dir.file_name) - 1);
    new_dir.size = BLOCK_SIZE;
    new_dir.first_blk = free_block;
    new_dir.type = TYPE_DIR;
    new_dir.access_rights = READ | WRITE;

    bool added = false;
    for (auto& entry : parent_dir) {
        if (entry.file_name[0] == '\0') {
            entry = new_dir;
            added = true;
            break;
        }
    }

    if (!added) {
        std::cout << "No space available. \n";
        return -1;
    }

    dir_entry new_dir_content[BLOCK_SIZE / sizeof(dir_entry)];
    std::memset(new_dir_content, 0, sizeof(new_dir_content));

    std::strncpy(new_dir_content[0].file_name, ".", sizeof(new_dir_content[0].file_name) - 1);
    new_dir_content[0].first_blk = free_block;
    new_dir_content[0].type = TYPE_DIR;

    std::strncpy(new_dir_content[1].file_name, "..", sizeof(new_dir_content[1].file_name) - 1);
    new_dir_content[1].first_blk = parent_block;
    new_dir_content[1].type = TYPE_DIR;

    disk.write(free_block, reinterpret_cast<uint8_t*>(new_dir_content));
    disk.write(parent_block, reinterpret_cast<uint8_t*>(parent_dir));
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    return 0;
}

int FS::cd(std::string dirpath) {
    if (dirpath.back() == '/') dirpath.pop_back();

    uint16_t resolved_block;
    if (resolvePath(dirpath, resolved_block) != 0) {
        std::cout << "Dir not found. \n";
        return -1;
    }

    dir_entry resolved_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(resolved_block, reinterpret_cast<uint8_t*>(resolved_dir));

    bool is_directory = false;
    for (const auto& entry : resolved_dir) {
        if (std::strcmp(entry.file_name, ".") == 0 && entry.type == TYPE_DIR) {
            is_directory = true;
            break;
        }
    }

    current_directory_block = resolved_block;
    if (dirpath == ".." && !current_path.empty()) {
        current_path.pop_back();
    } else if (dirpath != "..") {
        if (dirpath[0] == '/') {
            current_path.clear();
        }
        current_path.push_back(dirpath);
    }

    std::cout << "Changed to directory '" << dirpath << "'.\n";
    return 0;
}

int FS::pwd(){
    std::cout << pwdToString() << std::endl;
    return 0;
}

int FS::chmod(std::string accessrights, std::string filepath) {
    if (accessrights.size() != 1 || accessrights[0] < '0' || accessrights[0] > '7') {
        std::cout << "Number must be between 0-7. \n";
        return -1;
    }

    uint8_t new_access_rights = static_cast<uint8_t>(accessrights[0] - '0');
    uint16_t search_block = current_directory_block;
    std::string file_name = filepath;

    if (filepath.find('/') != std::string::npos) {
        size_t last_slash = filepath.find_last_of('/');
        std::string dir_path = filepath.substr(0, last_slash);
        file_name = filepath.substr(last_slash + 1);

        if (file_name.empty()) {
            std::cout << "No empty name. \n";
            return -1;
        }

        if (resolvePath(dir_path.empty() ? "." : dir_path, search_block) != 0) {
            std::cout << "Cant handle path. \n";
            return -1;
        }
    }

    dir_entry dir_entries[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(search_block, reinterpret_cast<uint8_t*>(dir_entries));

    dir_entry* target_entry = nullptr;
    for (auto& entry : dir_entries) {
        if (std::strcmp(entry.file_name, file_name.c_str()) == 0) {
            target_entry = &entry;
            break;
        }
    }

    if (!target_entry) {
        std::cout << "File/dir not found. \n";
        return -1;
    }

    if (target_entry->type != TYPE_FILE) {
        std::cout << "Target is not a file. \n";
        return -1;
    }

    target_entry->access_rights = new_access_rights;
    disk.write(search_block, reinterpret_cast<uint8_t*>(dir_entries));

    std::cout << "Access rights for '" << file_name << "' updated to " << (int)new_access_rights << ".\n";
    return 0;
}


// HELPER FUNCTION
int FS::resolvePath(const std::string& path, uint16_t& resolved_block) {
    if (path.empty()) return -1;

    std::vector<std::string> components;
    uint16_t current_block = (path[0] == '/') ? ROOT_BLOCK : current_directory_block;
    std::stringstream ss(path);

    std::string component;
    while (std::getline(ss, component, '/')) {
        if (component.empty() || component == ".") continue;
        if (component == "..") {
            dir_entry current_dir[BLOCK_SIZE / sizeof(dir_entry)];
            disk.read(current_block, reinterpret_cast<uint8_t*>(current_dir));
            bool found_parent = false;
            for (const auto& entry : current_dir) {
                if (std::strcmp(entry.file_name, "..") == 0 && entry.type == TYPE_DIR) {
                    current_block = entry.first_blk;
                    found_parent = true;
                    break;
                }
            }
            if (!found_parent) return -1;
        } else {
            dir_entry current_dir[BLOCK_SIZE / sizeof(dir_entry)];
            disk.read(current_block, reinterpret_cast<uint8_t*>(current_dir));
            bool found = false;
            for (const auto& entry : current_dir) {
                if (std::strcmp(entry.file_name, component.c_str()) == 0 && entry.type == TYPE_DIR) {
                    current_block = entry.first_blk;
                    found = true;
                    break;
                }
            }
            if (!found) return -1;
        }
    }

    resolved_block = current_block;
    return 0;
}

int FS::resolvefilePath(const std::string& path, uint16_t& resolved_block, dir_entry& file_entry) {
    if (path.empty()) return -1;

    size_t last_slash = path.find_last_of('/');
    std::string dir_path = (last_slash == std::string::npos) ? "." : path.substr(0, last_slash);
    std::string file_name = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);

    uint16_t parent_dir_block;
    if (resolvePath(dir_path, parent_dir_block) != 0)
        return -1; 
    

    dir_entry parent_dir[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(parent_dir_block, reinterpret_cast<uint8_t*>(parent_dir));

    for (const auto& entry : parent_dir) {
        if (std::strcmp(entry.file_name, file_name.c_str()) == 0 && entry.type == TYPE_FILE) {
            resolved_block = entry.first_blk; 
            file_entry = entry;
            return 0;
        }
    }

    return -1;
}

uint16_t FS::allocateBlocks(const std::vector<uint8_t>& file_data) {
    if (file_data.empty()) return FAT_FREE;

    int remaining_size = file_data.size();
    const uint8_t* data_ptr = file_data.data();
    uint16_t first_block = FAT_FREE, previous_block = FAT_FREE;

    while (remaining_size > 0) {
        uint16_t free_block = FAT_FREE;

        for (uint16_t i = 0; i < FAT_SIZE; ++i) {
            if (fat[i] == FAT_FREE) {
                free_block = i;
                break;
            }
        }

        if (free_block == FAT_FREE) {
            std::cout << "No free blocks. \n";
            return FAT_FREE;
        }

        int bytes_to_write = std::min(remaining_size, BLOCK_SIZE);
        std::vector<uint8_t> temp_block(BLOCK_SIZE, 0);
        std::copy(data_ptr, data_ptr + bytes_to_write, temp_block.begin());
        disk.write(free_block, temp_block.data());
        remaining_size -= bytes_to_write;
        data_ptr += bytes_to_write;

        if (previous_block != FAT_FREE) {
            fat[previous_block] = free_block;
        } else {
            first_block = free_block;
        }
        previous_block = free_block;
    }

    if (previous_block != FAT_FREE) {
        fat[previous_block] = FAT_EOF;
    }

    return first_block;
}

bool FS::addToDirectory(uint16_t dir_block, const dir_entry& entry) {
    dir_entry entries[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(dir_block, reinterpret_cast<uint8_t*>(entries));

    for (const auto& existing : entries) {
        if (std::strcmp(existing.file_name, entry.file_name) == 0) {
            if (existing.type == TYPE_DIR) {
                std::cout << "Dir with that name already exissts. \n";
                return false;
            }
        }
    }

    for (auto& slot : entries) {
        if (slot.file_name[0] == '\0') {
            slot = entry;
            disk.write(dir_block, reinterpret_cast<uint8_t*>(entries));
            return true;
        }
    }

    std::cout << "No space in the dir. \n";
    return false;
}

bool FS::removeFromDirectory(uint16_t directory_block, const std::string& file_name) {
    dir_entry directory[BLOCK_SIZE / sizeof(dir_entry)];
    disk.read(directory_block, reinterpret_cast<uint8_t*>(directory));

    for (auto& entry : directory) {
        if (std::strcmp(entry.file_name, file_name.c_str()) == 0) {
            std::memset(&entry, 0, sizeof(dir_entry));
            disk.write(directory_block, reinterpret_cast<uint8_t*>(directory));
            return true;
        }
    }

    std::cout << "File not found. \n";
    return false;
}

std::string FS::pwdToString() {
    if (current_path.empty()) return "/";
    std::ostringstream path;
    for (const auto& dir : current_path) {
        path << "/" << dir;
    }
    return path.str();
}
