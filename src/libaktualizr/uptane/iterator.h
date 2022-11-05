#ifndef AKTUALIZR_UPTANE_ITERATOR_H_
#define AKTUALIZR_UPTANE_ITERATOR_H_

#include "fetcher.h"
#include "imagerepository.h"
#include "utilities/flow_control.h"

namespace Uptane {

Targets getTrustedDelegation(const Role &delegate_role, const Targets &parent_targets,
                             const ImageRepository &image_repo, INvStorage &storage, IMetadataFetcher &fetcher,
                             bool offline, const api::FlowControlToken *flow_control);

class LazyTargetsList {
 public:
  struct DelegatedTargetTreeNode {
    Role role{Role::Targets()};
    DelegatedTargetTreeNode *parent{nullptr};
    std::vector<std::shared_ptr<DelegatedTargetTreeNode>>::size_type parent_idx{0};
    std::vector<std::shared_ptr<DelegatedTargetTreeNode>> children;
  };

  class DelegationIterator {
    using iterator_category = std::input_iterator_tag;
    using value_type = Uptane::Target;
    using difference_type = int;
    using pointer = Uptane::Target *;
    using reference = Uptane::Target &;

   public:
    explicit DelegationIterator(const ImageRepository &repo, std::shared_ptr<INvStorage> storage,
                                std::shared_ptr<Uptane::Fetcher> fetcher, const api::FlowControlToken *flow_control,
                                bool is_end = false);
    DelegationIterator operator++();
    bool operator==(const DelegationIterator &other) const;
    bool operator!=(const DelegationIterator &other) const { return !(*this == other); }
    const Uptane::Target &operator*();

   private:
    void renewTargetsData();

    std::shared_ptr<DelegatedTargetTreeNode> tree_;
    DelegatedTargetTreeNode *tree_node_;
    const ImageRepository &repo_;
    std::shared_ptr<INvStorage> storage_;
    std::shared_ptr<Fetcher> fetcher_;
    const api::FlowControlToken *flow_control_;
    std::shared_ptr<const Targets> cur_targets_;
    std::vector<Targets>::size_type target_idx_{0};
    std::vector<std::shared_ptr<DelegatedTargetTreeNode>>::size_type children_idx_{0};
    bool terminating_{false};
    int level_{0};
    bool is_end_;
  };

  explicit LazyTargetsList(const ImageRepository &repo, std::shared_ptr<INvStorage> storage,
                           std::shared_ptr<Fetcher> fetcher, const api::FlowControlToken *flow_control)
      : repo_{repo}, storage_{std::move(storage)}, fetcher_{std::move(fetcher)}, flow_control_{flow_control} {}
  DelegationIterator begin() { return DelegationIterator(repo_, storage_, fetcher_, flow_control_); }
  DelegationIterator end() { return DelegationIterator(repo_, storage_, fetcher_, flow_control_, true); }

 private:
  const ImageRepository &repo_;
  std::shared_ptr<INvStorage> storage_;
  std::shared_ptr<Uptane::Fetcher> fetcher_;
  const api::FlowControlToken *flow_control_;
};
}  // namespace Uptane

#endif  // AKTUALIZR_UPTANE_ITERATOR_H_
