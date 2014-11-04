// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GPG_MANAGER_H
#define GPG_MANAGER_H

#include "common.h"

namespace fpl {

class GPGManager {
 public:
  GPGManager();

  // Start of initial initialization and auth.
  bool Initialize(bool ui_login);

  // Call once a frame to allow us to track our async work.
  void Update();

  // To be called from UI to sign out (if we were signed in) or sign back in
  // (if we were signed out).
  void ToggleSignIn();

  // Logged in status, can be shown in UI.
  bool LoggedIn();

  struct GPGIds { const char *leaderboard, *event; };

  // Request this stat to be saved for the logged in
  // player. Does nothing if not logged in.
  void SaveStat(const char *event_id, uint64_t *score);

  void ShowLeaderboards(const GPGIds *ids, size_t id_len);
  void ShowAchievements();

 private:
  // These are the states the manager can be in, in sequential order they
  // are expected to happen.
  enum AsyncState {
    kStart,
    kAutoAuthStarted,
    kAutoAuthFailed,
    kManualSignBackIn,
    kAuthUILaunched,
    kAuthUIStarted,
    kAuthUIFailed,
    kAuthed,
  };

  AsyncState state_;
  bool do_ui_login_;
  std::unique_ptr<gpg::GameServices> game_services_;
};

}  // fpl

#endif  // GPG_MANAGER_H
