# frozen_String_literal: true

module ESP32
  module WiFi
    module Auth
      OPEN = 0
      WEP = 2
      WPA_PSK = 3
      WPA2_PSK = 4
      WPA_WPA2_PSK = 5
      WPA2_ENTERPRISE = 6
    end

    class AccessPoint
      class Error < StandardError; end
      class NoSSIDError < Error; end
      class UnspecifiedNumberOfMaxConnectionsError < Error; end
      class InvalidMaxConnectionCountError < Error; end
      class UnsupportedAuthMethodError < Error; end
      class PSKRequiredByAuthMethod < Error; end
      class InvalidChannelError < Error; end

      AUTH = ESP32::WiFi::Auth
      DEFAULT_MAX_CONNECTIONS = 1
      DEFAULT_ENCRYPTION_METHOD = AUTH::OPEN
      DEFAULT_HIDDEN = false
      SUPPORTED_AUTH_METHODS = [
        AUTH::OPEN,
        AUTH::WEP,
        AUTH::WPA_PSK,
        AUTH::WPA2_PSK,
        AUTH::WPA_WPA2_PSK
      ].freeze
      AUTH_METHODS_THAT_REQUIRE_A_PSK = [
        AUTH::WEP,
        AUTH::WPA_PSK,
        AUTH::WPA2_PSK,
        AUTH::WPA_WPA2_PSK
      ].freeze

      attr_accessor :ssid
      attr_accessor :max_connections
      attr_accessor :channel
      attr_accessor :hidden
      attr_accessor :encryption

      def start(options = {})
        ssid = extract_ssid(options)
        max_connections = extract_max_connections(options)
        auth_method = extract_auth_method(options)
        psk = extract_psk(options, auth_method)
        channel = extract_channel(options)
        hidden = extract_hidden(options)

        __start(ssid, auth_method, psk, max_connections, channel, hidden)
      end

      def stop
        __stop
      end

      private

      def extract_ssid(options)
        options[:ssid] || ssid || raise(NoSSIDError)
      end

      def extract_max_connections(options)
        connections_count =
          options[:max_connections] ||
          max_connections ||
          DEFAULT_MAX_CONNECTIONS

        unless connections_count.is_a?(Integer)
          raise(UnspecifiedNumberOfMaxConnectionsError)
        end

        raise(InvalidMaxConnectionCountError) if connections_count.negative?

        connections_count
      end

      def extract_auth_method(options)
        method =
          (options[:encryption] && options[:encryption][:method]) ||
          encryption[:method] ||
          DEFAULT_ENCRYPTION_METHOD

        unless SUPPORTED_AUTH_METHODS.include?(method)
          raise(UnsupportedAuthMethodError)
        end

        method
      end

      def extract_psk(options, auth_method)
        psk =
          (options[:encryption] && options[:encryption][:psk]) ||
          encryption[:psk]

        if AUTH_METHODS_THAT_REQUIRE_A_PSK.include?(auth_method) && psk.nil?
          raise(PSKRequiredByAuthMethod)
        end

        psk
      end

      def extract_channel(options)
        channel = options[:channel] || self.channel

        return 0 unless channel

        raise(InvalidChannelError) unless channel.is_a?(Integer)
        raise(InvalidChannelError) if channel < 1
        raise(InvalidChannelError) if channel > 14

        channel
      end

      def extract_hidden(options)
        options[:hidden] || hidden || DEFAULT_HIDDEN
      end
    end

    class Station
      attr_accessor :ssid
      attr_accessor :password
    end
  end
end
