#ifndef PRIMARY_SECONDARY_CONFIG_H_
#define PRIMARY_SECONDARY_CONFIG_H_

#include <string>

namespace Primary {

class SecondaryConfig {
 public:
  explicit SecondaryConfig(std::string type) : type_(std::move(type)) {}
  virtual ~SecondaryConfig() = default;
  virtual std::string type() const { return type_; }

 protected:
  SecondaryConfig(const SecondaryConfig &) = default;
  SecondaryConfig(SecondaryConfig &&) = default;
  SecondaryConfig &operator=(const SecondaryConfig &) = default;
  SecondaryConfig &operator=(SecondaryConfig &&) = default;

 private:
  std::string type_;
};

}  // namespace Primary

#endif  // PRIMARY_SECONDARY_CONFIG_H_
