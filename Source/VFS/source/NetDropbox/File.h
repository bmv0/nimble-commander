// Copyright (C) 2017-2025 Michael Kazakov. Subject to GNU General Public License version 3.
#pragma once

#include "Host.h"
#include <deque>
#include <mutex>
#include <condition_variable>

@class NCVFSDropboxFileDownloadDelegate;
@class NCVFSDropboxFileUploadStream;
@class NCVFSDropboxFileUploadDelegate;

namespace nc::vfs::dropbox {

class File final : public VFSFile
{
public:
    File(std::string_view _relative_path, const std::shared_ptr<class DropboxHost> &_host);
    ~File();

    std::expected<void, Error> Open(unsigned long _open_flags, const VFSCancelChecker &_cancel_checker) override;
    std::expected<void, Error> Close() override;
    std::expected<size_t, Error> PreferredIOSize() const override;
    bool IsOpened() const override;
    ReadParadigm GetReadParadigm() const override;
    WriteParadigm GetWriteParadigm() const override;
    std::expected<size_t, Error> Read(void *_buf, size_t _size) override;
    std::expected<size_t, Error> Write(const void *_buf, size_t _size) override;
    std::expected<uint64_t, Error> Pos() const override;
    std::expected<uint64_t, Error> Size() const override;
    bool Eof() const override;
    std::expected<void, Error> SetUploadSize(size_t _size) override;
    std::expected<void, Error> SetChunkSize(size_t _size);

private:
    /**
     * Download flow: Cold -> Initiated -> Downloading -> (Canceled|Completed)
     * Upload flow:   Cold -> Initiated -> Uploading ->   (Canceled|Completed)
     */
    enum State {
        Cold = 0,
        Initiated = 1,
        Downloading = 2,
        Uploading = 3, // Initiated upload switches to Uploading in SetUploadSize()
        Canceled = 4,
        Completed = 5,
        StatesAmount
    };

    void CheckStateTransition(State _new_state) const;
    void SwitchToState(State _new_state);
    ssize_t FeedUploadTaskAsync(uint8_t *_buffer, size_t _sz);
    bool HasDataToFeedUploadTaskAsync() const;
    void AppendDownloadedDataAsync(NSData *_data);
    void HandleDownloadResponseAsync(ssize_t _download_size);
    void HandleDownloadError(const Error &_error);
    void StartSmallUpload();
    void StartSession();
    void StartSessionAppend();
    void StartSessionFinish();
    NSURLRequest *BuildDownloadRequest() const;
    NSURLRequest *BuildRequestForSinglePartUpload() const;
    NSURLRequest *BuildRequestForUploadSessionInit() const;
    NSURLRequest *BuildRequestForUploadSessionAppend() const;
    NSURLRequest *BuildRequestForUploadSessionFinish() const;
    std::string BuildUploadPathspec() const;
    const DropboxHost &DropboxHost() const;
    ssize_t WaitForUploadBufferConsumption() const;
    void PushUploadDataIntoFIFOAndNotifyStream(const void *_buf, size_t _size);
    void ExtractSessionIdOrCancelUploadAsync(NSData *_data);
    void WaitForSessionIdOrError() const;
    void WaitForDownloadResponse() const;
    void WaitForAppendToComplete() const;

    struct Download {
        std::deque<uint8_t> fifo;
        long fifo_offset = 0; // is it always equal to m_FilePos???
        NSURLSessionDataTask *task;
        NCVFSDropboxFileDownloadDelegate *delegate = nil;
    };
    struct Upload {
        std::deque<uint8_t> fifo;
        std::atomic_long fifo_offset{0};
        long upload_size = -1;
        bool partitioned = false;
        int part_no = 0;
        int parts_count = 0;
        NSURLSessionUploadTask *task = nil;
        NCVFSDropboxFileUploadDelegate *delegate = nil;
        NCVFSDropboxFileUploadStream *stream = nil;
        std::string session_id;
        std::atomic_bool append_accepted{false};
    };

    unsigned long m_OpenFlags = 0;
    long m_FilePos = 0;
    long m_FileSize = -1;
    long m_ChunkSize = 150 * 1000 * 1000; // 150Mb according to dropbox docs

    std::atomic<State> m_State{Cold};

    mutable std::mutex m_SignalLock;
    mutable std::condition_variable m_Signal;

    mutable std::mutex m_DataLock;        // any access to m_Download/m_Upload must be guarded
    std::unique_ptr<Download> m_Download; // exists only on reading
    std::unique_ptr<Upload> m_Upload;     // exists only on writing

    std::optional<Error> m_LastError;
};

} // namespace nc::vfs::dropbox
