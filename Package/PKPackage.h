#pragma once

#include <cstdint>
#include <vector>
#include <fstream>
#include <stdexcept>

#include "CMChunk.h"
#include <string>

class PKPackage
{
public:
    std::vector<CMChunk> rootChunks; // Root chunks of the package
    bool isLittleEndian = false;

public:
    PKPackage() = default;

    uint32_t GetPackageSize() const
    {
        return sizeof(PKPackage);
    }

    void ReadFromFile(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);

        if (!file.is_open())
            throw std::runtime_error("Failed to open package file: " + filePath);

        rootChunks.clear();

        while (file.peek() != EOF)
        {
            rootChunks.push_back(CMChunk::Read(file, isLittleEndian));
        }

        file.close();
    }
};