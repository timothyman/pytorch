#include <torch/csrc/autograd/functions/utils.h>
#include <torch/csrc/distributed/autograd/context/dist_autograd_container.h>
#include <torch/csrc/distributed/autograd/functions/recvrpc_backward.h>
#include <torch/csrc/distributed/autograd/functions/sendrpc_backward.h>
#include <torch/csrc/distributed/autograd/utils.h>
#include <torch/csrc/distributed/rpc/rpc_agent.h>

namespace torch {
namespace distributed {
namespace autograd {

using torch::distributed::autograd::AutogradMetadata;
using torch::distributed::autograd::RpcWithAutograd;
using torch::distributed::rpc::FutureMessage;
using torch::distributed::rpc::Message;
using torch::distributed::rpc::MessageType;
using torch::distributed::rpc::RpcAgent;
using torch::distributed::rpc::RpcCommandBase;
using torch::distributed::rpc::WorkerInfo;

void addSendRpcBackward(
    DistAutogradContext& autogradContext,
    const AutogradMetadata& autogradMetadata,
    std::vector<torch::Tensor>& tensors,
    const rpc::worker_id_t dst) {
  // Attach the appropriate autograd edges.
  auto grad_fn = std::make_shared<SendRpcBackward>();
  grad_fn->set_next_edges(torch::autograd::collect_next_edges(tensors));

  // Add the appropriate input metadata for the grad_fn.
  for (const auto& tensor : tensors) {
    grad_fn->add_input_metadata(tensor);
  }

  // Record the send autograd function in our current context.
  autogradContext.addSendFunction(grad_fn, autogradMetadata.autogradMessageId);
  // Record the workerID
  autogradContext.addKnownWorkerId(dst);
}

DistAutogradContext* addRecvRpcBackward(
    const AutogradMetadata& autogradMetadata,
    std::vector<torch::Tensor>& tensors,
    rpc::worker_id_t fromWorkerId) {
  TORCH_INTERNAL_ASSERT(
      torch::autograd::compute_requires_grad(tensors),
      "Received tensors do not require grad, addRecvRpcBackward should not be called");
  // Initialize autograd context if necessary.
  auto& autogradContainer = DistAutogradContainer::getInstance();
  DistAutogradContext& autogradContext =
      autogradContainer.getOrCreateContext(autogradMetadata.autogradContextId);

  // Attach the tensors as inputs to the autograd function.
  auto grad_fn = std::make_shared<RecvRpcBackward>(
      autogradMetadata, autogradContext, fromWorkerId);
  for (auto& tensor : tensors) {
    torch::autograd::set_history(tensor, grad_fn);
  }

  // Now update the autograd context with the necessary information.
  autogradContext.addRecvFunction(grad_fn, autogradMetadata.autogradMessageId);
  return &autogradContext;
}

Message getMessageWithAutogradCheck(
    const rpc::worker_id_t dstId,
    torch::distributed::rpc::Message&& wrappedRpcMsg,
    MessageType msgType) {
  auto& autogradContainer = DistAutogradContainer::getInstance();

  if (!autogradContainer.hasValidContext() ||
      !torch::autograd::compute_requires_grad(wrappedRpcMsg.tensors())) {
    return std::move(wrappedRpcMsg);
  }

  // Retrieve the appropriate context to modify.
  auto& autogradContext = autogradContainer.currentContext();

  // Wrap the original rpc with autograd information.
  AutogradMetadata autogradMetadata(
      autogradContext.contextId(), autogradContainer.newAutogradMessageId());
  auto rpcWithAutograd = c10::guts::make_unique<RpcWithAutograd>(
      RpcAgent::getDefaultRpcAgent()->getWorkerInfo().id_,
      msgType,
      autogradMetadata,
      std::move(wrappedRpcMsg));

  // Record autograd information for 'send'.
  addSendRpcBackward(
      autogradContext, autogradMetadata, rpcWithAutograd->tensors(), dstId);

  return std::move(*rpcWithAutograd).toMessage();
}

std::shared_ptr<FutureMessage> sendMessage(
    RpcAgent& agent,
    const WorkerInfo& dst,
    torch::distributed::rpc::Message&& wrappedRpcMsg,
    MessageType msgType) {
  auto msg =
      getMessageWithAutogradCheck(dst.id_, std::move(wrappedRpcMsg), msgType);

  return agent.send(dst, std::move(msg));
}

} // namespace autograd
} // namespace distributed
} // namespace torch
