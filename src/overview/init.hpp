#pragma once

namespace overview {

// Initialize overview system (config, hooks, callbacks, monitors)
void init();

// Cleanup overview system
void exit();

// Dispatchers for overview
void addDispatchers();

} // namespace overview
