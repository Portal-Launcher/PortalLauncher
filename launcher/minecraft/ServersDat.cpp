// SPDX-License-Identifier: GPL-3.0-only
#include "ServersDat.h"

#include <QFile>
#include <QSet>

#include <sstream>

#include <io/stream_reader.h>
#include <io/stream_writer.h>
#include <tag_compound.h>
#include <tag_list.h>
#include <tag_string.h>

#include "FileSystem.h"

namespace ServersDat {

namespace {

std::unique_ptr<nbt::tag_compound> parseFile(const QString& path)
{
    try {
        QByteArray input = FS::read(path);
        std::istringstream in(std::string(input.constData(), input.size()));
        auto pair = nbt::io::read_compound(in);
        if (pair.first != "" || pair.second == nullptr)
            return nullptr;
        return std::move(pair.second);
    } catch (...) {
        return nullptr;
    }
}

bool writeFile(const QString& path, nbt::tag_compound& root)
{
    try {
        if (!FS::ensureFilePathExists(path))
            return false;
        std::ostringstream out;
        nbt::io::write_tag("", root, out);
        QByteArray bytes(out.str().data(), static_cast<int>(out.str().size()));
        FS::write(path, bytes);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

QList<Entry> read(const QString& serversDatPath)
{
    QList<Entry> out;
    if (!QFile::exists(serversDatPath))
        return out;
    auto root = parseFile(serversDatPath);
    if (!root || !root->has_key("servers", nbt::tag_type::List))
        return out;
    try {
        auto& list = (*root)["servers"].as<nbt::tag_list>();
        for (auto& value : list) {
            if (value.get_type() != nbt::tag_type::Compound)
                continue;
            auto& server = value.as<nbt::tag_compound>();
            Entry entry;
            if (server.has_key("ip", nbt::tag_type::String))
                entry.address = QString::fromUtf8(std::string(server["ip"]).c_str());
            if (server.has_key("name", nbt::tag_type::String))
                entry.name = QString::fromUtf8(std::string(server["name"]).c_str());
            if (!entry.address.trimmed().isEmpty())
                out.append(entry);
        }
    } catch (...) {
        return {};
    }
    return out;
}

int mergeAppend(const QString& serversDatPath, const QList<Entry>& entries)
{
    if (entries.isEmpty())
        return 0;

    std::unique_ptr<nbt::tag_compound> root;
    if (QFile::exists(serversDatPath)) {
        root = parseFile(serversDatPath);
        if (!root)
            return -1;  // never clobber a file we cannot parse
    } else {
        root = std::make_unique<nbt::tag_compound>();
    }

    try {
        if (!root->has_key("servers", nbt::tag_type::List))
            root->insert("servers", nbt::value(nbt::tag_list()));
        auto& list = root->at("servers").as<nbt::tag_list>();

        QSet<QString> present;
        for (auto& value : list) {
            if (value.get_type() != nbt::tag_type::Compound)
                continue;
            auto& server = value.as<nbt::tag_compound>();
            if (server.has_key("ip", nbt::tag_type::String))
                present.insert(QString::fromUtf8(std::string(server["ip"]).c_str()).trimmed().toLower());
        }

        int added = 0;
        for (const auto& entry : entries) {
            const QString key = entry.address.trimmed().toLower();
            if (key.isEmpty() || present.contains(key))
                continue;
            present.insert(key);
            nbt::tag_compound server;
            server.insert("name", entry.name.trimmed().isEmpty() ? std::string("Minecraft Server")
                                                                 : entry.name.trimmed().toUtf8().toStdString());
            server.insert("ip", entry.address.trimmed().toUtf8().toStdString());
            list.push_back(std::move(server));
            added++;
        }
        if (added == 0)
            return 0;
        return writeFile(serversDatPath, *root) ? added : -1;
    } catch (...) {
        return -1;
    }
}

}  // namespace ServersDat
