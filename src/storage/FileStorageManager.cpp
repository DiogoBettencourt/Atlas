#include "atlas/storage/FileStorageManager.hpp"

#include <fstream>

namespace atlas::storage {

namespace fs = std::filesystem;

FileStorageManager::FileStorageManager(fs::path data_root) : data_root_(std::move(data_root)) {
    std::error_code ec;
    fs::create_directories(data_root_, ec);
}

fs::path FileStorageManager::pathFor(const std::string& key) const {
    // Keys are namespaced with '/'; treat each segment as a directory
    // component and append a ".json" suffix to the final segment.
    fs::path relative(key);
    fs::path full = data_root_ / relative;
    full += ".json";
    return full;
}

void FileStorageManager::save(const std::string& key, const nlohmann::json& document) {
    std::lock_guard lock(mutex_);
    fs::path target = pathFor(key);
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);

    // Write atomically via a temp file + rename to avoid truncated files if
    // the process is interrupted mid-write.
    fs::path tmp = target;
    tmp += ".tmp";

    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("FileStorageManager: unable to open '" + tmp.string() +
                                  "' for writing");
    }
    out << document.dump(2);
    out.close();

    fs::rename(tmp, target, ec);
    if (ec) {
        // Fallback for cross-filesystem moves where rename() may fail.
        fs::copy_file(tmp, target, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
    }
}

std::optional<nlohmann::json> FileStorageManager::load(const std::string& key) const {
    std::lock_guard lock(mutex_);
    fs::path target = pathFor(key);
    std::ifstream in(target);
    if (!in) {
        return std::nullopt;
    }
    try {
        nlohmann::json doc;
        in >> doc;
        return doc;
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
}

bool FileStorageManager::exists(const std::string& key) const {
    std::lock_guard lock(mutex_);
    return fs::exists(pathFor(key));
}

bool FileStorageManager::remove(const std::string& key) {
    std::lock_guard lock(mutex_);
    std::error_code ec;
    return fs::remove(pathFor(key), ec);
}

std::vector<std::string> FileStorageManager::listKeys(const std::string& prefix) const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> results;
    fs::path dir = data_root_ / prefix;
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return results;
    }
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            fs::path stem = entry.path().stem();
            results.push_back((fs::path(prefix) / stem).generic_string());
        }
    }
    return results;
}

std::optional<std::chrono::system_clock::time_point> FileStorageManager::lastModified(
    const std::string& key) const {
    std::lock_guard lock(mutex_);
    std::error_code ec;
    auto file_time = fs::last_write_time(pathFor(key), ec);
    if (ec) {
        return std::nullopt;
    }
    // file_clock -> system_clock. std::chrono::clock_cast would do this
    // directly but isn't available in every standard library we build on,
    // so re-base against "now" on both clocks instead (good to well under
    // a second, which is plenty for a "last active" timestamp).
    auto offset = std::chrono::system_clock::now().time_since_epoch() -
                  fs::file_time_type::clock::now().time_since_epoch();
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            file_time.time_since_epoch() + offset));
}

} // namespace atlas::storage
