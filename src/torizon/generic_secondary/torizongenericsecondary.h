#ifndef PRIMARY_TORIZONGENERICSECONDARY_H_
#define PRIMARY_TORIZONGENERICSECONDARY_H_

#include <json/json.h>
#include <string>

#include "gtest/gtest_prod.h"
#include "libaktualizr/types.h"
#include "managedsecondary.h"

namespace Primary {

class TorizonGenericSecondaryConfig : public ManagedSecondaryConfig {
 public:
  // FIXME: [TORIZON] If upstreaming is to be done then this could be "managed-generic"
  static constexpr const char* const Type{"torizon-generic"};

  TorizonGenericSecondaryConfig() : ManagedSecondaryConfig(Type) {}
  explicit TorizonGenericSecondaryConfig(const Json::Value& json_config);

  static std::vector<TorizonGenericSecondaryConfig> create_from_file(const boost::filesystem::path& file_full_path);
  void dump(const boost::filesystem::path& file_full_path) const;

  boost::filesystem::path action_handler_path;
};

class TorizonGenericSecondary : public ManagedSecondary {
 public:
  explicit TorizonGenericSecondary(const Primary::TorizonGenericSecondaryConfig& sconfig_in);
  std::string Type() const override { return TorizonGenericSecondaryConfig::Type; }

  bool ping() const override { return true; }

  // Main methods being overridden from `ManagedSecondary`.
  bool getFirmwareInfo(Uptane::InstalledImageInfo& firmware_info) const override;
  data::InstallationResult install(const Uptane::Target& target, const InstallInfo& info) override;
  boost::optional<data::InstallationResult> completePendingInstall(const Uptane::Target& target) override {
    return completeInstall(target);
  }

 protected:
  using VarMap = std::unordered_map<std::string, std::string>;

  enum class ActionHandlerResult {
    NotAvailable,   ///< No result available.
    ReqNormalProc,  ///< Action-handler requests normal processing for the action.
    ReqErrorProc,   ///< Action-handler requests error processing for the action.
    ProcNoOutput,   ///< Action was processed by action-handler, no/bad output available.
    ProcOutput,     ///< Action was processed by action-handler, output available.
    Default = ReqErrorProc
  };

  /**
   * Invoke the action-handler passing it the desired action and environment
   * variables and process its output.
   *
   * @param action The action to be executed; directly forwarded to the
   *  action-handler.
   * @param action_vars A map with environment variables to be passed to the
   *  action-handler; these will be merged with the current process
   *  environment and the shared variables returned by getSharedVars().
   * @param output Pointer to Json::Value whose contents will be set from the
   *  parsed output of the action handler; this will be populated only when
   *  the method returns ActionHandlerResult::Processed (and the pointer is
   *  not null).
   * @return The result of calling the action-handler.
   */
  ActionHandlerResult callActionHandler(const std::string& action, const VarMap& action_vars,
                                        Json::Value* output = nullptr) const;

  /**
   * Get a map with the environment variables shared by all actions.
   *
   * @param update Whether or not the internally cached value of the variables
   *  should be updated.
   */
  const VarMap& getSharedVars(bool update = true) const;

  void getInstallVars(VarMap& vars, const Uptane::Target& target, const InstallInfo& info) const;
  void getCompleteInstallVars(VarMap& vars, const Uptane::Target& target) const;

  data::InstallationResult completeInstall(const Uptane::Target& target);
  void maybeFinishInstall(data::ResultCode::Numeric result_code, const boost::filesystem::path& new_fwpath,
                          const boost::filesystem::path& new_tgtname);

  boost::filesystem::path getNewFirmwarePath() const;
  boost::filesystem::path getNewTargetNamePath() const;

  mutable VarMap shared_vars_;
  TorizonGenericSecondaryConfig config_;

  friend class TorizonGenericSecondaryTest;
  FRIEND_TEST(TorizonGenericSecondaryTest, NonExistingHandler);
  FRIEND_TEST(TorizonGenericSecondaryTest, HandlerFinishedBySignal);
  FRIEND_TEST(TorizonGenericSecondaryTest, NoHandlerOutputExpected);
  FRIEND_TEST(TorizonGenericSecondaryTest, HandlerOutputExpected);
  FRIEND_TEST(TorizonGenericSecondaryTest, CompleteInstallFailure);
  FRIEND_TEST(TorizonGenericSecondaryTest, CompleteInstallSuccess);
};

}  // namespace Primary

#endif  // PRIMARY_TORIZONGENERICSECONDARY_H_
