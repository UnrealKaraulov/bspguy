#pragma once
#include <iostream>
#include <string>
#include <fstream>

class JackWriter {
public:
	JackWriter(const std::string& filename) : file(filename, std::ios::binary) {}

	~JackWriter() {
		if (file.is_open()) {
			file.close();
		}
	}

	template <typename T>
	void write(const T& value) {
		file.write(reinterpret_cast<const char*>(&value), sizeof(T));
	}

	void writeLenStr(const std::string& str) {
		int size = (int)str.length();
		//write(size);
		write<int>(size);
		if (size > 0)
			file.write(str.data(), size);
	}

	void writeKeyVal(const std::string& key, const std::string& val)
	{
		writeLenStr(key);
		writeLenStr(val);
	}

	bool is_open() const {
		return file && file.is_open();
	}

private:
	std::ofstream file;
};

class JackReader {
public:
	JackReader(const std::string& filename) : file(filename, std::ios::binary) {}

	~JackReader() {
		if (file.is_open()) {
			file.close();
		}
	}

	template <typename T>
	bool read(T& value) {
		file.read(reinterpret_cast<char*>(&value), sizeof(T));
		return file.good();
	}

	bool readLenStr(std::string& str) {
		int size = 0;
		if (!read<int>(size)) {
			return false;
		}
		str.resize(size);
		if (size > 0) {
			file.read(&str[0], size);
		}
		return file.good();
	}

	bool readKeyVal(std::string& key, std::string& val) {
		return readLenStr(key) && readLenStr(val);
	}

	bool is_open() const {
		return file && file.is_open();
	}

private:
	std::ifstream file;
};

