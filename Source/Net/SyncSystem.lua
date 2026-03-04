local M = {}

local function forward(fn)
	return function(...)
		return fn(...)
	end
end

function M.create(bindings)
	bindings = bindings or {}
	return {
		splitPacketFields = forward(assert(bindings.splitPacketFields, "splitPacketFields binding required")),
		makeBlobTransferKey = forward(assert(bindings.makeBlobTransferKey, "makeBlobTransferKey binding required")),
		queueModelTransfer = forward(assert(bindings.queueModelTransfer, "queueModelTransfer binding required")),
		requestModelFromPeers = forward(assert(bindings.requestModelFromPeers, "requestModelFromPeers binding required")),
		updatePeerVisualModel = forward(assert(bindings.updatePeerVisualModel, "updatePeerVisualModel binding required")),
		handleModelRequestPacket = forward(assert(bindings.handleModelRequestPacket, "handleModelRequestPacket binding required")),
		handleModelMetaPacket = forward(assert(bindings.handleModelMetaPacket, "handleModelMetaPacket binding required")),
		handleModelChunkPacket = forward(assert(bindings.handleModelChunkPacket, "handleModelChunkPacket binding required")),
		pumpModelTransfers = forward(assert(bindings.pumpModelTransfers, "pumpModelTransfers binding required")),
		updateNet = forward(assert(bindings.updateNet, "updateNet binding required")),
		serviceNetworkEvents = forward(assert(bindings.serviceNetworkEvents, "serviceNetworkEvents binding required")),
		updateCloudNetworkState = forward(assert(bindings.updateCloudNetworkState, "updateCloudNetworkState binding required"))
	}
end

return M
