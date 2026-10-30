#pragma once

#include "engine/document/WriteGrant.hpp"

/// The files a command line is told to write; the command line keeps no history.
struct CliWrites
{
    static WriteGrant to(const QString &path) { return WriteGrant(path); }
};
