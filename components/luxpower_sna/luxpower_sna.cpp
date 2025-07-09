// components/luxpower_sna/luxpower_sna.cpp

// ... (keep everything above this function the same) ...

void LuxpowerSNAComponent::request_data_() {
  if (this->tcp_client_ != nullptr && this->tcp_client_->connected()) {
    ESP_LOGD(TAG, "Skipping data request, TCP client is busy.");
    return;
  }
  
  ESP_LOGD(TAG, "Connecting to %s:%u", this->host_.c_str(), this->port_);
  this->tcp_client_ = new AsyncClient();

  // CORRECTED onData LAMBDA SIGNATURE
  this->tcp_client_->onData([this](void *arg, AsyncClient *client, void *data, size_t len) {
    this->handle_packet_(data, len);
    client->close(); // Use the client pointer from the callback
  });

  this->tcp_client_->onConnect([this](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "TCP connected, sending heartbeat frame.");
    // Heartbeat Frame: 0xA1, 0x1A, 0x01, 0x02, 0x00, 0x0F, {dongle_serial}, 0x00, 0x00
    std::vector<uint8_t> request;
    request.insert(request.end(), {0xA1, 0x1A, 0x01, 0x02, 0x00, 0x0F});
    request.insert(request.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    request.insert(request.end(), {0x00, 0x00});
    client->write(reinterpret_cast<const char*>(request.data()), request.size());

    // Data Frame: 0xA1, 0x1A, 0x01, 0x02, 0x00, 0x12, {dongle_serial}, {inverter_serial}, 0x00, 0x00
    request.clear();
    request.insert(request.end(), {0xA1, 0x1A, 0x01, 0x02, 0x00, 0x12});
    request.insert(request.end(), this->dongle_serial_.begin(), this->dongle_serial_.end());
    request.insert(request.end(), this->inverter_serial_.begin(), this->inverter_serial_.end());
    request.insert(request.end(), {0x00, 0x00});
    client->write(reinterpret_cast<const char*>(request.data()), request.size());
  });

  this->tcp_client_->onError([this](void *arg, AsyncClient *client, int8_t error) {
    ESP_LOGE(TAG, "TCP connection error: %s", client->errorToString(error));
  });

  this->tcp_client_->onTimeout([this](void *arg, AsyncClient *client, uint32_t time) {
    ESP_LOGW(TAG, "TCP connection timeout.");
    client->close();
  });

  this->tcp_client_->onDisconnect([this](void *arg, AsyncClient *client) {
    ESP_LOGD(TAG, "TCP disconnected.");
    delete this->tcp_client_;
    this->tcp_client_ = nullptr;
  });

  this->tcp_client_->connect(this->host_.c_str(), this->port_);
}

// ... (keep everything below this function the same) ...
