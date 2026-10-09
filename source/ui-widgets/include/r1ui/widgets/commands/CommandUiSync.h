// Copyright (c) 2026 R1GUI. All rights reserved. Proprietary.
// Owns: CommandUiSync, the one place that keeps command-driven widgets live: it subscribes to the
//   registry (so a rebound chord, a reset or an import reaches menus and toolbars at once, decision
//   D15) and re-runs every attached refresher plus the refresh of open command menus.
// Why: enabled and checked answers are predicates the application changes without telling anyone;
//   chords change through the overrides. A binding cannot poll itself (nothing may keep a timer armed
//   on a settled screen), so the host calls refresh() once per frame (cheap) and the registry version
//   triggers an immediate refresh.
// Callers: the host (construct, call refresh() per frame), bindCommandToolbar and the gallery page
//   (attach refreshers). Calls: CommandServices, refreshOpenCommandMenus.
// Lifetime: the sync must be destroyed before the UiContext it was given (it refreshes open menus
//   through it). Bindings keep a weak reference to the hub, so destroying the sync first is safe (the
//   bindings simply stop refreshing); a refresher is detached by its owner's destructor. refresh() is
//   not re-entrant: a refresh requested while one runs is ignored (the running one already sees the
//   latest state because refreshers re-read everything).
// Threading: UI thread only.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "r1ui/widgets/commands/CommandServices.h"

namespace r1ui::widgets {

class CommandUiSync {
 public:
  using Refresher = std::function<void()>;
  using Token = uint32_t;

  // A handle a binding keeps to detach itself; safe after the sync is gone.
  class Hub;
  class Attachment {
   public:
    Attachment() = default;
    Attachment(std::weak_ptr<Hub> hub, Token token) : hub_(std::move(hub)), token_(token) {}
    Attachment(Attachment&& other) noexcept : hub_(std::move(other.hub_)), token_(std::exchange(other.token_, 0)) {}
    Attachment& operator=(Attachment&& other) noexcept;
    Attachment(const Attachment&) = delete;
    Attachment& operator=(const Attachment&) = delete;
    ~Attachment() { reset(); }
    void reset();

   private:
    std::weak_ptr<Hub> hub_;
    Token token_ = 0;
  };

  CommandUiSync(UiContext& ui, CommandServices services);
  ~CommandUiSync();
  CommandUiSync(const CommandUiSync&) = delete;
  CommandUiSync& operator=(const CommandUiSync&) = delete;

  // Adds a refresher; it is removed when the returned attachment is destroyed or reset.
  Attachment attach(Refresher refresher);
  // Refreshes open command menus and every attached refresher.
  void refresh();
  uint64_t refreshCount() const;
  const CommandServices& services() const { return services_; }

 private:
  UiContext& ui_;
  CommandServices services_;
  std::shared_ptr<Hub> hub_;
  commands::CommandRegistry::ListenerId listener_ = 0;
};

}  // namespace r1ui::widgets
