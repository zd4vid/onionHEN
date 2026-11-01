#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace onion::plugin_ui {

enum class NodeKind : uint16_t {
  Page = 1,
  Menu = 2,
  Group = 3,
  Label = 4,
  Action = 5,
  Toggle = 6,
  List = 7,
  ListItem = 8,
  Input = 9,
};

enum class ValueType : uint32_t {
  None = 0,
  Bool = 1,
  Integer = 2,
  String = 3,
};

enum class BindingKind : uint32_t {
  None = 0,
  Config = 1,
  Event = 2,
};

struct Node {
  NodeKind kind = NodeKind::Label;
  uint32_t flags = 0;
  ValueType value_type = ValueType::None;
  BindingKind binding = BindingKind::None;
  int64_t min_value = 0;
  int64_t max_value = 0;
  uint32_t min_length = 0;
  uint32_t max_length = 0;
  std::string id;
  std::string parent_id;
  std::string title;
  std::string description;
  std::string target_id;
  std::string binding_key;
  std::string value;
};

struct Document {
  uint32_t flags = 0;
  int32_t priority = 0;
  std::string plugin_id;
  std::string contribution_id;
  std::string title;
  std::string description;
  std::string root_page_id;
  std::vector<Node> nodes;

  const Node *find_node(std::string_view id) const;
  Node *find_node(std::string_view id);
};

enum class Status {
  Ok = 0,
  InvalidArgument,
  InvalidEncoding,
  InvalidDocument,
  PermissionDenied,
  NotFound,
  Stale,
  InvalidState,
  NotSupported,
  Busy,
};

using Handle = uint64_t;

struct ContributionSnapshot {
  Handle handle = 0;
  std::string owner;
  std::shared_ptr<const Document> document;
};

struct RegistrySnapshot {
  uint64_t generation = 0;
  std::vector<ContributionSnapshot> contributions;

  const ContributionSnapshot *find(Handle handle) const;
};

struct RegisterResult {
  Status status = Status::InvalidArgument;
  Handle handle = 0;
  uint64_t generation = 0;
  std::string error;
};

class Registry {
public:
  RegisterResult register_encoded(std::string_view owner,
                                  std::span<const uint8_t> encoded);
  Status unregister(std::string_view owner, Handle handle);
  size_t remove_owner(std::string_view owner);
  Status set_value(std::string_view owner, Handle handle,
                   std::string_view node_id, ValueType value_type,
                   std::string_view value);
  RegistrySnapshot snapshot() const;

private:
  mutable std::mutex mutex_;
  uint64_t generation_ = 0;
  Handle next_handle_ = 1;
  std::vector<ContributionSnapshot> contributions_;
};

using TransferId = uint32_t;

struct BeginResult {
  Status status = Status::InvalidArgument;
  TransferId transfer_id = 0;
};

class RegistrationBroker {
public:
  explicit RegistrationBroker(Registry &registry) : registry_(registry) {}

  BeginResult begin(std::string_view owner, uint32_t total_size,
                    uint32_t checksum);
  Status append(std::string_view owner, TransferId transfer_id,
                uint32_t offset, std::span<const uint8_t> chunk);
  RegisterResult commit(std::string_view owner, TransferId transfer_id);
  Status abort(std::string_view owner, TransferId transfer_id);
  size_t disconnect(std::string_view owner);

private:
  struct Transfer {
    TransferId id = 0;
    std::string owner;
    uint32_t checksum = 0;
    std::vector<uint8_t> bytes;
    size_t received = 0;
  };

  Registry &registry_;
  std::mutex mutex_;
  TransferId next_transfer_id_ = 1;
  std::vector<Transfer> transfers_;
};

} // namespace onion::plugin_ui
