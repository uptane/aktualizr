#ifndef METAFAKE_H_
#define METAFAKE_H_

#include <boost/filesystem/path.hpp>

/**
 * Fill a directory with fake Uptane metadata
 * @param meta_dir
 */
void CreateFakeRepoMetaData(boost::filesystem::path meta_dir);

#endif  // METAFAKE_H

// vim: set tabstop=2 shiftwidth=2 expandtab:
