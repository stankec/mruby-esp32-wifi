# frozen_String_literal: true

module ESP32
  module WiFi
    class Station
      attr_accessor :ssid
      attr_accessor :password
    end

    class AccessPoint
      attr_accessor :ssid
      attr_accessor :password
      attr_accessor :max_connections
      attr_accessor :auth_mode
    end
  end
end
