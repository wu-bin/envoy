#include "extensions/common/tap/extension_config_base.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {

ExtensionConfigBase::ExtensionConfigBase(
    const envoy::config::common::tap::v2alpha::CommonExtensionConfig proto_config,
    TapConfigFactoryPtr&& config_factory, Server::Admin& admin,
    Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_thread_dispatcher)
    : proto_config_(proto_config), config_factory_(std::move(config_factory)),
      tls_slot_(tls.allocateSlot()) {
  // TODO(mattklein123): Admin is the only supported config type currently. Validated by schema.
  // fixfix
  ASSERT(proto_config_.has_admin_config());

  admin_handler_ = AdminHandler::getSingleton(admin, singleton_manager, main_thread_dispatcher);
  admin_handler_->registerConfig(*this, proto_config_.admin_config().config_id());
  ENVOY_LOG(debug, "initializing tap extension with admin endpoint (config_id={})",
            proto_config_.admin_config().config_id());

  tls_slot_->set([](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<TlsFilterConfig>();
  });
}

ExtensionConfigBase::~ExtensionConfigBase() {
  if (admin_handler_) {
    admin_handler_->unregisterConfig(*this);
  }
}

const std::string& ExtensionConfigBase::adminId() {
  ASSERT(proto_config_.has_admin_config());
  return proto_config_.admin_config().config_id();
}

void ExtensionConfigBase::clearTapConfig() {
  tls_slot_->runOnAllThreads([this] { tls_slot_->getTyped<TlsFilterConfig>().config_ = nullptr; });
}

void ExtensionConfigBase::newTapConfig(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                                       Sink* admin_streamer) {
  TapConfigSharedPtr new_config =
      config_factory_->createConfigFromProto(std::move(proto_config), admin_streamer);
  tls_slot_->runOnAllThreads(
      [this, new_config] { tls_slot_->getTyped<TlsFilterConfig>().config_ = new_config; });
}

} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
