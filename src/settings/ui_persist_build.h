#pragma once

#include "app_state.h"
#include "settings/nvs_store.h"

UiPersist BuildUiPersistFromState(const AppState& state);
