// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#pragma once

#include <QObject>
#include <QString>
#include <functional>

/** Shrinks resource packs with PackSquash before they are uploaded to a
 *  shared instance, so friends download less and every push costs less
 *  bandwidth (the service re-requests every non-Modrinth file on each push).
 *
 *  PackSquash (AGPL-3.0) ships next to the launcher and runs as a separate
 *  program, so its license stays its own. Only lossless optimizations are
 *  enabled: textures and sounds come out byte-for-byte equivalent in game,
 *  just packed better. Originals are never touched, results are cached by
 *  source hash, and any failure quietly falls back to the original file.
 */
namespace PackSquash {

/** Path to the bundled binary, or empty when it is not present. */
QString binaryPath();
bool isAvailable();

/** True for the pack kinds PackSquash understands (it wants a pack.mcmeta).
 *  Shader packs are not resource packs, so they are left alone. */
bool canOptimize(const QString& fileType);

/**
 * Optimizes a pack zip, calling back with the path to upload. The callback
 * receives the original path unchanged whenever optimization is disabled,
 * unavailable, or unsuccessful, so callers never need a fallback path.
 *
 * @param sourceSha1 hash of the original zip; used as the cache key.
 */
void optimize(QObject* ctx, const QString& zipPath, const QString& sourceSha1, std::function<void(QString)> callback);

}  // namespace PackSquash
