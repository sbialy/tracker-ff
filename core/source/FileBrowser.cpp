#include "FileBrowser.h"
#include "Platform.h"

#include "util_string.h"

#include <string.h>

void FileBrowser::start(FileSystem* fileSystem, FileBrowserData* fileBrowserData)
{
    assert(fileBrowserData->browserList != nullptr);
    assert(
      !(fileBrowserData->selectionEnabled == true && fileBrowserData->selectionList == nullptr));

    this->fileSystem = fileSystem;
    data = fileBrowserData;

    refresh();
}

void FileBrowser::refresh()
{
    int listNamesOffset = 0;

    if (!isRoot())
    {
        data->browserList[0] = (char*)"/..";
        listNamesOffset = 1;
    }

    if (data->selectionEnabled)
    {
        assert(data->selectionList != nullptr);

        memset(data->selectionList, 0, data->maxBrowserListLen);
    }

    int listNamesLen = listNamesOffset;

    if (data->mode == kProjectBrowser || data->mode == kSamplesBrowser || data->mode == kModBrowser)
    {
        if (folderList != nullptr)
        {
            delete[] folderList;
            folderList = nullptr;
        }

        folderListLength = data->maxBrowserListLen - listNamesOffset;
        folderList = new char*[folderListLength];

        fileSystem->listDirContents(
          data->currentPath,
          FileSystem::DirContentType::kFolders,
          [](const char* name) { return strcasecmp(name, "workspace") != 0; },
          data->fileNamesBuffer,
          data->fileNamesBufferSize,
          kMaxFileNameLen,
          folderListLength,
          folderList);

        // check is projects - selection list
        if (data->selectionEnabled && data->mode == kProjectBrowser)
        {
            assert(data->selectionList != nullptr);
            if (folderListLength > 0)
            {
                char path[kMaxFilePathLen + 1];
                volatile int dstSize = sizeof(path);
                int selectionIdx = listNamesOffset;
                data->selectionList[0] = 0;
                for (int folder = 0; folder < folderListLength; folder++)
                {
                    snprintf(
                      path, dstSize, "%s%s/project.mt", data->currentPath, folderList[folder]);
                    data->selectionList[selectionIdx++] = fileSystem->exists(path);
                }
            }
        }

        memcpy(
          &data->browserList[listNamesLen], folderList, folderListLength * sizeof(folderList[0]));

        delete[] folderList;
        folderList = nullptr;

        // add valid folders
        listNamesLen += folderListLength;
        assert(listNamesLen <= data->maxBrowserListLen);
    }

    if (data->mode == kSamplesBrowser || data->mode == kModBrowser)
    {
        if (fileList != nullptr)
        {
            delete[] fileList;
            fileList = nullptr;
        }

        fileListLength = data->maxBrowserListLen - folderListLength - listNamesOffset;
        fileList = new char*[fileListLength];

        fileSystem->listDirContents(
          data->currentPath,
          FileSystem::DirContentType::kFiles,
          [&](const char* name) mutable
          {
              if (data->mode == kSamplesBrowser)
              {
                  return checkFilenameExtension(name, kWavFileExtension);
              }
              if (data->mode == kModBrowser)
              {
                  return checkFilenameExtension(name, kModFileExtension1) ||
                         checkFilenameExtension(name, kModFileExtension2);
              }
              return false;
          },
          data->fileNamesBuffer + (data->maxFileNameLen * folderListLength),
          data->fileNamesBufferSize - (data->maxFileNameLen * folderListLength),
          kMaxFileNameLen,
          fileListLength,
          fileList);

        if (data->selectionEnabled && data->mode == kModBrowser)
        {
            assert(data->selectionList != nullptr);

            if (fileListLength > 0)
            {
                data->selectionList[0] = 0;
                for (int position = listNamesLen; position < listNamesLen + fileListLength;
                     position++)
                {
                    data->selectionList[position] = 1;
                }
            }
        }

        memcpy(&data->browserList[listNamesLen], fileList, fileListLength * sizeof(fileList[0]));

        delete[] fileList;
        fileList = nullptr;

        // add valid files
        listNamesLen += fileListLength;
        assert(listNamesLen <= data->maxBrowserListLen);
    }

    data->browserListLength = listNamesLen;

    if (data->browserListPosition >= data->browserListLength)
    {
        data->browserListPosition = 0;
    }
}

void FileBrowser::selectSpecificFolderName(char* folderNameToSelect)
{
    // set position on specific folder name
    if (folderNameToSelect != nullptr)
    {
        for (int listPos = 0; listPos < data->browserListLength; listPos++)
        {
            if (data->browserList[listPos][0] == '/' &&
                strcmp(folderNameToSelect, data->browserList[listPos] + 1) == 0)
            {
                data->browserListPosition = listPos;
                break;
            }
        }
    }
}

void FileBrowser::resetPath()
{
    polystrncpy(data->currentPath, data->defaultPath, kMaxFilePathLen + 1);
    refresh();
}

void FileBrowser::processCurrentPathFileList(char* /*buffer*/, int /*bufferSize*/, int /*maxNamesCount*/)
{
}

void FileBrowser::processCurrentPathFolderList(char* /*buffer*/,
                                               int /*bufferSize*/,
                                               int /*maxNamesCount*/,
                                               int /*selectionListOffset*/)
{
}

bool FileBrowser::isRoot()
{
    return data->currentPath[0] == 0;
}

bool FileBrowser::isFolderSelected()
{
    return isFolder(data->browserListPosition);
}

bool FileBrowser::isFolder(int position)
{
    if (!isRoot())
    {
        if (position == 0)
        {
            return false;
        }
        else
        {
            position--;
        }
    }

    return position < folderListLength;
}

bool FileBrowser::isPositionOnSelectionList()
{
    //    if (!isRoot() && (data->browserListPosition == 0))
    //    {
    //        return false;
    //    }

    return data->selectionList[data->browserListPosition];
}

void FileBrowser::browse()
{
    if ((data->browserListPosition == 0) && (!isRoot()))
    {
        openParentFolder();
    }
    else if (isFolderSelected())
    {
        openFolder(data->browserListPosition);
    }
}

void FileBrowser::openFolder(int position)
{
    assert(isFolder(position));

    if (isRoot())
    {
        char* folderName = data->browserList[position];
        assert(folderName[0] != 0);
        snprintf(data->currentPath,
                 sizeof(data->currentPath),
                 "%s",
                 folderName[0] == '/' ? &folderName[1] : folderName);
    }
    else
    {
        assert(position > 0);

        char tempPath[kMaxFilePathLen];
        snprintf(
          tempPath, sizeof(tempPath), "%s%s", data->currentPath, data->browserList[position]);
        polystrncpy(data->currentPath, tempPath, kMaxFilePathLen);
    }

    data->browserListPosition = 0;

    refresh();
}

void FileBrowser::openParentFolder()
{
    assert(!isRoot());

    char parentName[kMaxFilePathLen];
    getParentFolderName(parentName);

    int sourcePathLen = strlen(data->currentPath);
    int nameStart = sourcePathLen;

    while (nameStart > 0 && data->currentPath[nameStart] == '/')
    {
        nameStart--;
    }

    while (nameStart > 0 && data->currentPath[nameStart] != '/')
    {
        nameStart--;
    }

    if (nameStart > 0)
    {
        data->currentPath[nameStart] = 0;
    }
    else
    {
        data->currentPath[0] = 0;
    }

    refresh();
    selectSpecificFolderName(parentName);
}

void FileBrowser::getParentFolderName(char* name)
{
    int sourcePathLen = strlen(data->currentPath);
    int nameEnd = sourcePathLen;

    while (nameEnd > 0 && data->currentPath[nameEnd] == '/')
    {
        nameEnd--;
    }

    int nameStart = nameEnd;

    while (nameStart > 0 && data->currentPath[nameStart] != '/')
    {
        nameStart--;
    }

    if (nameStart > 0)
    {
        nameStart++;
    }

    strncpy(name, &data->currentPath[nameStart], nameEnd - nameStart);
    name[nameEnd - nameStart] = 0;
}

void FileBrowser::getSelectedItemWithPath(char* path, int maxLen)
{
    assert(data->browserListPosition < data->browserListLength);
    int currentPathLen = strlen(data->currentPath);
    assert(currentPathLen + 3 < maxLen);

    char* itemName = data->browserList[data->browserListPosition];
    assert(itemName[0] != 0);

    itemName = itemName[0] == '/' ? &itemName[1] : itemName;

    if (currentPathLen == 0)
    {
        strncpy(path, itemName, maxLen);
        path[maxLen - 1] = 0;
    }
    else
    {
        snprintf(path, maxLen, "%s/%s", data->currentPath, itemName);
    }
}

char* FileBrowser::getSelectedItemName()
{
    assert(data->browserListPosition < data->browserListLength);

    return data->browserList[data->browserListPosition];
}

char* FileBrowser::getSelectedFolderName()
{
    assert(data->browserListPosition < data->browserListLength);
    assert(data->browserList[data->browserListPosition][0] != 0);

    return &data->browserList[data->browserListPosition][1];
}

char* FileBrowser::getCurrentPath()
{
    return data->currentPath;
}

bool FileBrowser::checkFilenameExtension(const char* filename, const char* extension)
{
    return hasFileExtension(filename, extension);
}

bool FileBrowser::createFolderInCurrentPath(char* folderName)
{
    char path[kMaxFilePathLen];
    volatile int dstSize = sizeof(path);
    snprintf(path, dstSize, "%s/%s", data->currentPath, folderName);

    if (!fileSystem->exists(path))
    {
        fileSystem->mkdir(0, path);
        return true;
    }

    return false;
}

int FileBrowser::getPathFolderLevel(char* path)
{
    int level = 0;
    int pathLen = strlen(path);

    while (pathLen > 0)
    {
        while (pathLen > 0 && path[pathLen] == '/')
        {
            pathLen--;
        }

        while (pathLen > 0 && path[pathLen] != '/')
        {
            pathLen--;
        }
        level++;
    }

    return level;
}
