#include "core/Mesh.hpp"

#include "core/File.hpp"

#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace raiden {
namespace {

struct FaceCorner {
    int position = -1;
    int uv = -1;
    int normal = -1;

    bool operator<(const FaceCorner& other) const {
        return std::tie(position, uv, normal) < std::tie(other.position, other.uv, other.normal);
    }
};

FaceCorner parseCorner(const std::string& token) {
    FaceCorner corner;
    std::string part;
    std::istringstream in(token);
    int field = 0;
    while (std::getline(in, part, '/')) {
        if (!part.empty()) {
            const int index = std::stoi(part);
            if (field == 0) {
                corner.position = index;
            } else if (field == 1) {
                corner.uv = index;
            } else if (field == 2) {
                corner.normal = index;
            }
        }
        ++field;
    }
    return corner;
}

int resolveIndex(int index, int count, const char* what) {
    if (index > 0) {
        return index - 1;
    }
    if (index < 0) {
        return count + index;
    }
    throw std::runtime_error(std::string("OBJ ") + what + " index is zero");
}

}  // namespace

Mesh Mesh::loadObj(const std::filesystem::path& path) {
    const std::string text = readText(path);
    std::istringstream stream(text);

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;

    Mesh mesh;
    std::map<FaceCorner, std::uint32_t> remap;

    const auto addCorner = [&](const FaceCorner& raw) -> std::uint32_t {
        if (const auto it = remap.find(raw); it != remap.end()) {
            return it->second;
        }
        Vertex vertex{};
        if (raw.position != -1) {
            const int i = resolveIndex(raw.position, static_cast<int>(positions.size()), "position");
            vertex.position = positions.at(static_cast<std::size_t>(i));
        }
        if (raw.normal != -1) {
            const int i = resolveIndex(raw.normal, static_cast<int>(normals.size()), "normal");
            vertex.normal = normals.at(static_cast<std::size_t>(i));
        }
        if (raw.uv != -1) {
            const int i = resolveIndex(raw.uv, static_cast<int>(uvs.size()), "uv");
            vertex.uv = uvs.at(static_cast<std::size_t>(i));
        }
        const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(vertex);
        remap.emplace(raw, index);
        return index;
    };

    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            glm::vec3 p{};
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (tag == "vt") {
            glm::vec2 t{};
            ss >> t.x >> t.y;
            uvs.push_back(t);
        } else if (tag == "vn") {
            glm::vec3 n{};
            ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (tag == "f") {
            std::vector<std::string> corners;
            std::string token;
            while (ss >> token) {
                corners.push_back(token);
            }
            if (corners.size() < 3) {
                throw std::runtime_error("OBJ face has fewer than 3 vertices in " + path.string());
            }
            const auto i0 = addCorner(parseCorner(corners[0]));
            for (std::size_t i = 1; i + 1 < corners.size(); ++i) {
                mesh.indices.push_back(i0);
                mesh.indices.push_back(addCorner(parseCorner(corners[i])));
                mesh.indices.push_back(addCorner(parseCorner(corners[i + 1])));
            }
        }
    }

    if (mesh.vertices.empty() || mesh.indices.empty()) {
        throw std::runtime_error("OBJ contained no triangles: " + path.string());
    }
    return mesh;
}

}  // namespace raiden
