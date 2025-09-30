#ifndef SOTA_CLIENT_TOOLS_OSTREE_OBJECT_H_
#define SOTA_CLIENT_TOOLS_OSTREE_OBJECT_H_

#include <chrono>
#include <iostream>
#include <list>
#include <sstream>

#include <curl/curl.h>
#include <boost/filesystem/path.hpp>
#include <boost/intrusive_ptr.hpp>
#include "gtest/gtest_prod.h"

#include "garage_common.h"
#include "ostree_hash.h"
#include "treehub_server.h"

class OSTreeRepo;
class RequestPool;

enum class PresenceOnServer { kObjectStateUnknown, kObjectPresent, kObjectMissing, kObjectInProgress };

enum class CurrentOp { kOstreeObjectUploading, kOstreeObjectPresenceCheck };

/**
 * Broad categories for the result of attempting an upload.
 * At the moment all errors from the server are considered temporary, because
 * we are unable to detect a server failure that is definitely permanent.
 */
enum class ServerResponse {
  /** The upload hasn't been attempted yet */
  kNoResponse,
  /** The upload was successful */
  kOk,
  /** There was an error uploading the object, but a retry may work */
  kTemporaryFailure,
};

class OSTreeObject {
 public:
  using ptr = boost::intrusive_ptr<OSTreeObject>;
  OSTreeObject(const OSTreeRepo& repo, OSTreeHash hash, OstreeObjectType object_type);
  OSTreeObject(const OSTreeObject&) = delete;
  OSTreeObject(OSTreeObject&&) = delete;
  OSTreeObject operator=(const OSTreeObject&) = delete;
  OSTreeObject operator=(OSTreeObject&&) = delete;

  ~OSTreeObject();

  /* This object has been uploaded, notify parents. If parent object has no more
   * children pending upload, add the parent to the upload queue. */
  void NotifyParents(RequestPool& pool);

  /* Send a HEAD request to the destination server to check if this object is
   * present there. */
  void MakeTestRequest(const TreehubServer& push_target, CURLM* curl_multi_handle);

  /* Upload this object to the destination server. */
  void Upload(TreehubServer& push_target, CURLM* curl_multi_handle, RunMode mode);

  /* Process a completed curl transaction (presence check or upload). */
  void CurlDone(CURLM* curl_multi_handle, RequestPool& pool);

  uintmax_t GetSize() const;

  PresenceOnServer is_on_server() const { return is_on_server_; }
  CurrentOp operation() const { return current_operation_; }
  bool children_ready() const { return children_.empty(); }
  void LaunchNotify() { is_on_server_ = PresenceOnServer::kObjectInProgress; }
  std::chrono::steady_clock::time_point RequestStartTime() const { return request_start_time_; }
  ServerResponse LastOperationResult() const { return last_operation_result_; }

  bool Fsck() const;

 private:
  using childiter = std::list<OSTreeObject::ptr>::iterator;
  using parentref = std::pair<OSTreeObject*, childiter>;

  /* Add parent to this object. */
  void AddParent(OSTreeObject* parent, std::list<OSTreeObject::ptr>::iterator parent_it);

  /* Child object of this object has been uploaded, remove it from the list. */
  void ChildNotify(std::list<OSTreeObject::ptr>::iterator child_it);

  /* If the child has is not already on the server, add it to this object's list
   * of children and add this object as the parent of the new child. */
  void AppendChild(const OSTreeObject::ptr& child);

  /* Parse this object for children. */
  void PopulateChildren();

  /* Add queries to the queue for any children whose presence on the server is
   * unknown. */
  void QueryChildren(RequestPool& pool);

  std::string Url() const;

  /* Check for children. If they are all present and this object isn't present,
   * upload it. If any children are missing, query them. */
  void CheckChildren(RequestPool& pool, long rescode);  // NOLINT(google-runtime-int)

  /* Handle an error from a presence check. */
  void PresenceError(RequestPool& pool, int64_t rescode);

  /* Handle an error from an upload. */
  void UploadError(RequestPool& pool, int64_t rescode);

  static size_t curl_handle_write(void* buffer, size_t size, size_t nmemb, void* userp);

  /** Full path on disk to this object */
  boost::filesystem::path PathOnDisk() const;

  FRIEND_TEST(OstreeObject, Request);
  FRIEND_TEST(OstreeObject, UploadDryRun);
  FRIEND_TEST(OstreeObject, UploadFail);
  FRIEND_TEST(OstreeObject, UploadSuccess);
  friend void intrusive_ptr_add_ref(OSTreeObject* /*h*/);
  friend void intrusive_ptr_release(OSTreeObject* /*h*/);
  friend std::ostream& operator<<(std::ostream& stream, const OSTreeObject& o);

  // SHA256 Hash of the object
  const OSTreeHash hash_;
  // Type of the object
  const OstreeObjectType type_;
  const OSTreeRepo& repo_;
  int refcount_;  // refcounts and intrusive_ptr are used to simplify
                  // interaction with curl
  PresenceOnServer is_on_server_;
  CurrentOp current_operation_{};

  std::stringstream http_response_;
  CURL* curl_handle_;
  FILE* fd_;
  std::list<parentref> parents_;
  std::list<OSTreeObject::ptr> children_;

  std::chrono::steady_clock::time_point request_start_time_;
  ServerResponse last_operation_result_{ServerResponse::kNoResponse};
};

OSTreeObject::ptr ostree_object_from_curl(CURL* curlhandle);

std::ostream& operator<<(std::ostream& stream, const OSTreeObject::ptr& o);

// vim: set tabstop=2 shiftwidth=2 expandtab:
#endif  // SOTA_CLIENT_TOOLS_OSTREE_OBJECT_H_
