#pragma once

#include "envoy/config/common/tap/v2alpha/common.pb.h"
#include "envoy/thread_local/thread_local.h"

#include "extensions/common/tap/admin.h"
#include "extensions/common/tap/tap.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

/**
 * fixfix
 */
class ExtensionConfigBase : public ExtensionConfig, Logger::Loggable<Logger::Id::tap> {
public:
  // Extensions::Common::Tap::ExtensionConfig
  void clearTapConfig() override;
  const std::string& adminId() override;
  void newTapConfig(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                    Sink* admin_streamer) override;

protected:
  ExtensionConfigBase(const envoy::config::common::tap::v2alpha::CommonExtensionConfig proto_config,
                      TapConfigFactoryPtr&& config_factory, Server::Admin& admin,
                      Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
                      Event::Dispatcher& main_thread_dispatcher);
  ~ExtensionConfigBase();

  template <class T> std::shared_ptr<T> currentConfigHelper() const {
    return std::dynamic_pointer_cast<T>(tls_slot_->getTyped<TlsFilterConfig>().config_);
  }

private:
  struct TlsFilterConfig : public ThreadLocal::ThreadLocalObject {
    TapConfigSharedPtr config_;
  };

  const envoy::config::common::tap::v2alpha::CommonExtensionConfig proto_config_;
  TapConfigFactoryPtr config_factory_;
  ThreadLocal::SlotPtr tls_slot_;
  AdminHandlerSharedPtr admin_handler_;
};

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
