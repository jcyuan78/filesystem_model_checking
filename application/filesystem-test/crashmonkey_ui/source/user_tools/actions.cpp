#include "../pch.h"
#include "actions.h"

#include "../utils/communication/ClientCommandSender.h"

namespace fs_testing {
namespace user_tools {
namespace api {

using fs_testing::utils::communication::ClientCommandSender;
using fs_testing::utils::communication::kSocketNameOutbound;
using fs_testing::utils::communication::SocketMessage;

int Checkpoint() {
  ClientCommandSender c(fs_testing::utils::communication::kSocketNameOutbound, SocketMessage::kCheckpoint,
      SocketMessage::kCheckpointDone);
  return c.Run();
}

} // fs_testing
} // user_tools
} // api
