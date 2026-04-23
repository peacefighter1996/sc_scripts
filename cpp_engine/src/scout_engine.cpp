#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

struct DataPoint {
    int id{};
    std::string server;
    double x{};
    double y{};
    double z{};
    std::string planet;
    std::string material;
    bool location{};
    double quality_min{};
    double quality_max{};
    std::string note;
};

static std::string trim(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

static std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static std::vector<std::string> split_csv_row(const std::string& line) {
    std::vector<std::string> values;
    std::string current;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (ch == ',' && !in_quotes) {
            values.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    values.push_back(current);
    return values;
}

static std::string csv_escape(const std::string& value) {
    const bool needs_quotes = value.find(',') != std::string::npos || value.find('"') != std::string::npos || value.find('\n') != std::string::npos || value.find('\r') != std::string::npos;
    if (!needs_quotes) {
        return value;
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

static std::vector<DataPoint> load_points(const std::string& csv_path) {
    std::vector<DataPoint> points;
    std::ifstream in(csv_path);
    if (!in.is_open()) {
        return points;
    }

    std::string line;
    bool first_row = true;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        if (first_row) {
            first_row = false;
            const auto lower = to_lower(line);
            if (lower.find("recordid") != std::string::npos || lower.find("id") != std::string::npos) {
                continue;
            }
        }

        auto row = split_csv_row(line);
        if (row.size() < 9) {
            continue;
        }

        DataPoint point;
        try {
            point.id = std::stoi(trim(row[0]));
            point.server = trim(row[1]);
            point.x = std::stod(trim(row[2]));
            point.y = std::stod(trim(row[3]));
            point.z = std::stod(trim(row[4]));
            point.planet = trim(row[5]);
            point.material = trim(row[6]);
            point.quality_min = std::stod(trim(row[7]));
            point.quality_max = std::stod(trim(row[8]));
            point.note = row.size() >= 10 ? row[9] : std::string{};
            const auto mat = to_lower(point.material);
            point.location = mat == "location" || mat == "cave";
            points.push_back(point);
        } catch (...) {
            continue;
        }
    }

    return points;
}

static bool append_point(const std::string& csv_path, const DataPoint& point) {
    const auto file_exists = std::filesystem::exists(csv_path);
    const auto parent = std::filesystem::path(csv_path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream out(csv_path, std::ios::app);
    if (!out.is_open()) {
        return false;
    }

    if (!file_exists) {
        out << "recordid,server,x,y,z,planet,material,quality_min,quality_max,note\n";
    }

    out << point.id << ','
        << csv_escape(point.server) << ','
        << std::setprecision(15) << point.x << ','
        << std::setprecision(15) << point.y << ','
        << std::setprecision(15) << point.z << ','
        << csv_escape(point.planet) << ','
        << csv_escape(point.material) << ','
        << std::setprecision(15) << point.quality_min << ','
        << std::setprecision(15) << point.quality_max << ','
        << csv_escape(point.note)
        << '\n';

    return true;
}

static void print_dump(const std::vector<DataPoint>& points) {
    for (const auto& point : points) {
        std::cout << point.id << '\t'
                  << point.server << '\t'
                  << std::setprecision(15) << point.x << '\t'
                  << std::setprecision(15) << point.y << '\t'
                  << std::setprecision(15) << point.z << '\t'
                  << point.planet << '\t'
                  << point.material << '\t'
                  << std::setprecision(15) << point.quality_min << '\t'
                  << std::setprecision(15) << point.quality_max << '\t'
                  << point.note
                  << '\n';
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: scout_engine <command> ...\n";
        return 2;
    }

    const std::string command = argv[1];

    if (command == "dump") {
        if (argc != 3) {
            std::cerr << "usage: scout_engine dump <csv_path>\n";
            return 2;
        }
        const auto points = load_points(argv[2]);
        print_dump(points);
        return 0;
    }

    if (command == "next-id") {
        if (argc != 3) {
            std::cerr << "usage: scout_engine next-id <csv_path>\n";
            return 2;
        }
        const auto points = load_points(argv[2]);
        int max_id = 0;
        for (const auto& point : points) {
            max_id = std::max(max_id, point.id);
        }
        std::cout << (max_id + 1) << '\n';
        return 0;
    }

    if (command == "append") {
        if (argc != 13) {
            std::cerr << "usage: scout_engine append <csv_path> <id> <server> <x> <y> <z> <planet> <material> <quality_min> <quality_max> <note>\n";
            return 2;
        }

        DataPoint point;
        try {
            point.id = std::stoi(argv[3]);
            point.server = argv[4];
            point.x = std::stod(argv[5]);
            point.y = std::stod(argv[6]);
            point.z = std::stod(argv[7]);
            point.planet = argv[8];
            point.material = argv[9];
            point.quality_min = std::stod(argv[10]);
            point.quality_max = std::stod(argv[11]);
            point.note = argv[12];
            const auto mat = to_lower(point.material);
            point.location = mat == "location" || mat == "cave";
        } catch (...) {
            std::cerr << "invalid append arguments\n";
            return 2;
        }

        if (!append_point(argv[2], point)) {
            std::cerr << "failed to append point\n";
            return 1;
        }
        return 0;
    }

    std::cerr << "unknown command: " << command << '\n';
    return 2;
}
