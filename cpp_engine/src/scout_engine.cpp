#include "scout_app.h"
#include "scout_core.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        return run_scout_app();
    }

    const std::string command = argv[1];
    if (command == "app") {
        return run_scout_app();
    }

    if (command == "dump") {
        if (argc != 3) {
            std::cerr << "usage: scout_engine dump <csv_path>\n";
            return 2;
        }
        print_dump(load_points(argv[2]));
        return 0;
    }

    if (command == "next-id") {
        if (argc != 3) {
            std::cerr << "usage: scout_engine next-id <csv_path>\n";
            return 2;
        }

        int max_id = 0;
        for (const auto& point : load_points(argv[2])) {
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

        try {
            DataPoint point;
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
            const auto material = to_lower(point.material);
            point.location = material == "location" || material == "cave";
            return append_point(argv[2], point) ? 0 : 1;
        } catch (...) {
            std::cerr << "invalid append arguments\n";
            return 2;
        }
    }

    if (command == "parse-xyz") {
        if (argc != 3) {
            std::cerr << "usage: scout_engine parse-xyz <ocr_text>\n";
            return 2;
        }

        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        if (!try_parse_xyz_from_ocr_text(argv[2], x, y, z)) {
            return 1;
        }

        std::cout << x << '\t' << y << '\t' << z << '\n';
        return 0;
    }

    if (command == "ocr-task") {
        if (argc != 3 && argc != 4) {
            std::cerr << "usage: scout_engine ocr-task [rock_or_empty] <ocr_text>\n";
            return 2;
        }

        const std::string rock = (argc == 4) ? argv[2] : std::string{};
        const std::string text = (argc == 4) ? argv[3] : argv[2];
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        const bool ok = try_parse_xyz_from_ocr_text(text, x, y, z);
        std::cout << rock << '\t';
        if (!ok) {
            std::cout << "nan\tnan\tnan\n";
            return 1;
        }
        std::cout << x << '\t' << y << '\t' << z << '\n';
        return 0;
    }

    if (command == "predict-labels-onnx") {
        if (argc != 4) {
            std::cerr << "usage: scout_engine predict-labels-onnx <model_path> <sample_count>\n";
            return 2;
        }

        int64_t sample_count = 0;
        try {
            sample_count = std::stoll(argv[3]);
        } catch (...) {
            std::cerr << "invalid sample_count\n";
            return 2;
        }

        std::vector<float> input_values;
        input_values.reserve(static_cast<size_t>(sample_count) * 14 * 9);
        float value = 0.0f;
        while (std::cin >> value) {
            input_values.push_back(value);
        }

        std::vector<int64_t> labels;
        std::string error_message;
        if (!predict_labels_onnx(argv[2], input_values, sample_count, labels, error_message)) {
            std::cerr << error_message << '\n';
            return 1;
        }

        for (size_t i = 0; i < labels.size(); ++i) {
            if (i > 0) {
                std::cout << ' ';
            }
            std::cout << labels[i];
        }
        std::cout << '\n';
        return 0;
    }

    std::cerr << "unknown command: " << command << '\n';
    return 2;
}
