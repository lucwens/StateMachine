/**
 * @file ThreadedHSM.hpp
 * @brief Threaded Hierarchical State Machine with Commands support
 *
 * Extends the basic HSM with:
 * - Commands (non-state-changing actions restricted to specific states)
 * - JSON message protocol for inter-thread communication
 * - Synchronous and asynchronous command execution
 * - Command buffering with futures/promises
 * - Thread-safe message queue
 *
 * JSON Message Protocol:
 * {
 *   "id": <unique_identifier>,
 *   "type": "event" | "command",
 *   "name": <event_or_command_name>,
 *   "params": { ... },
 *   "sync": true | false
 * }
 *
 * Response Format:
 * {
 *   "id": <same_identifier>,
 *   "success": true | false,
 *   "result": { ... } | null,
 *   "error": <error_message> | null
 * }
 */

#pragma once

#include "LaserTrackerHSM.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>

namespace LaserTracker
{
    // ============================================================================
    // JSON Message Types (Simple implementation without external dependencies)
    // ============================================================================

    /**
     * @brief Simple JSON-like value representation
     */
    struct JsonValue
    {
        using Object = std::unordered_map<std::string, JsonValue>;
        using Array  = std::vector<JsonValue>;
        using Value  = std::variant<std::nullptr_t, bool, int, double, std::string, Array, Object>;

        Value value;

        JsonValue() : value(nullptr) {}
        JsonValue(std::nullptr_t) : value(nullptr) {}
        JsonValue(bool b) : value(b) {}
        JsonValue(int i) : value(i) {}
        JsonValue(double d) : value(d) {}
        JsonValue(const char* s) : value(std::string(s)) {}
        JsonValue(const std::string& s) : value(s) {}
        JsonValue(std::string&& s) : value(std::move(s)) {}
        JsonValue(Array arr) : value(std::move(arr)) {}
        JsonValue(Object obj) : value(std::move(obj)) {}

        bool isNull() const { return std::holds_alternative<std::nullptr_t>(value); }
        bool isBool() const { return std::holds_alternative<bool>(value); }
        bool isInt() const { return std::holds_alternative<int>(value); }
        bool isDouble() const { return std::holds_alternative<double>(value); }
        bool isString() const { return std::holds_alternative<std::string>(value); }
        bool isArray() const { return std::holds_alternative<Array>(value); }
        bool isObject() const { return std::holds_alternative<Object>(value); }

        bool               asBool() const { return std::get<bool>(value); }
        int                asInt() const { return std::get<int>(value); }
        double             asDouble() const { return std::get<double>(value); }
        const std::string& asString() const { return std::get<std::string>(value); }
        const Array&       asArray() const { return std::get<Array>(value); }
        const Object&      asObject() const { return std::get<Object>(value); }
        Object&            asObject() { return std::get<Object>(value); }

        JsonValue& operator[](const std::string& key)
        {
            if (!isObject())
            {
                value = Object{};
            }
            return std::get<Object>(value)[key];
        }

        const JsonValue& at(const std::string& key) const { return std::get<Object>(value).at(key); }

        bool contains(const std::string& key) const
        {
            if (!isObject())
                return false;
            return std::get<Object>(value).count(key) > 0;
        }

        /**
         * @brief Serialize to JSON string
         */
        std::string toJson() const
        {
            return std::visit(
                [](const auto& v) -> std::string
                {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::nullptr_t>)
                    {
                        return "null";
                    }
                    else if constexpr (std::is_same_v<T, bool>)
                    {
                        return v ? "true" : "false";
                    }
                    else if constexpr (std::is_same_v<T, int>)
                    {
                        return std::to_string(v);
                    }
                    else if constexpr (std::is_same_v<T, double>)
                    {
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(6) << v;
                        return oss.str();
                    }
                    else if constexpr (std::is_same_v<T, std::string>)
                    {
                        return "\"" + escapeString(v) + "\"";
                    }
                    else if constexpr (std::is_same_v<T, Array>)
                    {
                        std::string result = "[";
                        for (size_t i = 0; i < v.size(); ++i)
                        {
                            if (i > 0)
                                result += ",";
                            result += v[i].toJson();
                        }
                        return result + "]";
                    }
                    else if constexpr (std::is_same_v<T, Object>)
                    {
                        std::string result = "{";
                        bool        first  = true;
                        for (const auto& [key, val] : v)
                        {
                            if (!first)
                                result += ",";
                            first = false;
                            result += "\"" + escapeString(key) + "\":" + val.toJson();
                        }
                        return result + "}";
                    }
                    return "null";
                },
                value);
        }

      private:
        static std::string escapeString(const std::string& s)
        {
            std::string result;
            for (char c : s)
            {
                switch (c)
                {
                    case '"':
                        result += "\\\"";
                        break;
                    case '\\':
                        result += "\\\\";
                        break;
                    case '\n':
                        result += "\\n";
                        break;
                    case '\r':
                        result += "\\r";
                        break;
                    case '\t':
                        result += "\\t";
                        break;
                    default:
                        result += c;
                }
            }
            return result;
        }
    };

    // ============================================================================
    // Unified Message Type
    // ============================================================================

    /**
     * @brief Unified message for both requests and responses
     *
     * Request fields:
     *   - id: Unique identifier for correlation
     *   - name: Name of the message (event/command name)
     *   - params: Parameters for the message
     *   - sync: If true, sender waits for result before processing next
     *   - timeoutMs: Timeout in milliseconds for reply (0 = no timeout)
     *   - timestamp: Creation time of the message
     *
     * Response fields (when isResponse=true):
     *   - success: True if executed successfully
     *   - result: Result data (stored in params)
     *   - error: Error message if failed
     *
     * The HSM determines whether a message triggers a state change or not.
     * Commands can also cause state changes (e.g., error conditions).
     */
    struct Message
    {
        using Clock     = std::chrono::steady_clock;
        using TimePoint = std::chrono::time_point<Clock>;

        uint64_t    id = 0;            // Unique identifier for correlation
        std::string name;              // Name of event/command
        JsonValue   params;            // Parameters (request) or result data (response)
        bool        sync       = false; // If true, sender waits for completion
        bool        needsReply = false; // If true, a response is expected
        uint32_t    timeoutMs  = 5000;  // Timeout in ms for reply (0 = no timeout)
        TimePoint   timestamp;          // When the message was created

        // Response-specific fields
        bool        isResponse = false; // True if this is a response message
        bool        success    = false; // True if executed successfully
        std::string error;              // Error message (if failed)

        Message() : timestamp(Clock::now()) {}

        /**
         * @brief Check if the message has timed out
         */
        bool isTimedOut() const
        {
            if (timeoutMs == 0)
                return false;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - timestamp);
            return elapsed.count() > timeoutMs;
        }

        /**
         * @brief Get remaining time until timeout
         */
        std::chrono::milliseconds remainingTime() const
        {
            if (timeoutMs == 0)
                return std::chrono::milliseconds::max();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - timestamp);
            auto remaining = static_cast<int64_t>(timeoutMs) - elapsed.count();
            return std::chrono::milliseconds(std::max<int64_t>(0, remaining));
        }

        /**
         * @brief Get age of message in milliseconds
         */
        uint64_t ageMs() const
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - timestamp).count();
        }

        /**
         * @brief Create a response from a request
         */
        static Message createResponse(uint64_t requestId, bool success, JsonValue result = {}, const std::string& error = "")
        {
            Message resp;
            resp.id         = requestId;
            resp.isResponse = true;
            resp.success    = success;
            resp.params     = std::move(result);
            resp.error      = error;
            return resp;
        }

        /**
         * @brief Create a timeout error response
         */
        static Message createTimeoutResponse(uint64_t requestId)
        {
            return createResponse(requestId, false, {}, "Request timed out");
        }

        std::string toJson() const
        {
            JsonValue obj = JsonValue::Object{};
            obj["id"]   = static_cast<int>(id);
            obj["name"] = name;
            obj["timestamp_ms"] = static_cast<int>(ageMs()); // Age since creation

            if (isResponse)
            {
                obj["isResponse"] = true;
                obj["success"]    = success;
                obj["result"]     = params;
                if (!error.empty())
                {
                    obj["error"] = error;
                }
            }
            else
            {
                obj["params"]    = params;
                obj["sync"]      = sync;
                obj["timeoutMs"] = static_cast<int>(timeoutMs);
            }
            return obj.toJson();
        }
    };

    // ============================================================================
    // Commands - Non-state-changing actions
    // ============================================================================

    namespace Commands
    {
        /**
         * @brief Home command - moves laser tracker to home position
         * Valid in: Idle state
         * Sync: Yes (waits for homing to complete)
         */
        struct Home
        {
            static constexpr const char* name = "Home";
            static constexpr bool        sync = true;

            // Parameters
            double speed = 100.0; // Homing speed percentage (0-100)

            std::string getName() const { return name; }
        };

        /**
         * @brief GetPosition command - retrieves current position
         * Valid in: Idle, Locked, Measuring states
         * Sync: No (returns immediately with current position)
         */
        struct GetPosition
        {
            static constexpr const char* name = "GetPosition";
            static constexpr bool        sync = false;

            std::string getName() const { return name; }
        };

        /**
         * @brief SetLaserPower command - adjusts laser power
         * Valid in: Any Operational state
         * Sync: No
         */
        struct SetLaserPower
        {
            static constexpr const char* name = "SetLaserPower";
            static constexpr bool        sync = false;

            double powerLevel = 1.0; // 0.0 to 1.0

            std::string getName() const { return name; }
        };

        /**
         * @brief Compensate command - applies environmental compensation
         * Valid in: Idle, Locked states
         * Sync: Yes (waits for compensation calculation)
         */
        struct Compensate
        {
            static constexpr const char* name = "Compensate";
            static constexpr bool        sync = true;

            double temperature = 20.0; // Celsius
            double pressure    = 1013.25; // hPa
            double humidity    = 50.0; // Percentage

            std::string getName() const { return name; }
        };

        /**
         * @brief GetStatus command - retrieves system status
         * Valid in: Any state
         * Sync: No
         */
        struct GetStatus
        {
            static constexpr const char* name = "GetStatus";
            static constexpr bool        sync = false;

            std::string getName() const { return name; }
        };

        /**
         * @brief MoveRelative command - moves tracker by relative amount
         * Valid in: Idle, Locked states
         * Sync: Yes (waits for move to complete)
         */
        struct MoveRelative
        {
            static constexpr const char* name = "MoveRelative";
            static constexpr bool        sync = true;

            double azimuth   = 0.0; // Degrees
            double elevation = 0.0; // Degrees

            std::string getName() const { return name; }
        };

    } // namespace Commands

    // Command variant - all possible commands
    using Command = std::variant<Commands::Home, Commands::GetPosition, Commands::SetLaserPower, Commands::Compensate, Commands::GetStatus,
                                 Commands::MoveRelative>;

    // ============================================================================
    // Thread-Safe Message Queue
    // ============================================================================

    /**
     * @brief Thread-safe FIFO queue for messages
     */
    template <typename T> class ThreadSafeQueue
    {
      public:
        void push(T item)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push_back(std::move(item));
            }
            cv_.notify_one();
        }

        void pushFront(T item)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push_front(std::move(item));
            }
            cv_.notify_one();
        }

        std::optional<T> tryPop()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty())
            {
                return std::nullopt;
            }
            T item = std::move(queue_.front());
            queue_.pop_front();
            return item;
        }

        T waitPop()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || stopped_; });
            if (stopped_ && queue_.empty())
            {
                throw std::runtime_error("Queue stopped");
            }
            T item = std::move(queue_.front());
            queue_.pop_front();
            return item;
        }

        std::optional<T> waitPopFor(std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty() || stopped_; }))
            {
                return std::nullopt;
            }
            if (stopped_ && queue_.empty())
            {
                return std::nullopt;
            }
            T item = std::move(queue_.front());
            queue_.pop_front();
            return item;
        }

        void stop()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopped_ = true;
            }
            cv_.notify_all();
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.empty();
        }

        size_t size() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }

        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.clear();
        }

      private:
        mutable std::mutex      mutex_;
        std::condition_variable cv_;
        std::deque<T>           queue_;
        bool                    stopped_ = false;
    };

    // ============================================================================
    // Pending Message Entry
    // ============================================================================

    struct PendingMessage
    {
        Message                   message;
        std::promise<Message>     promise; // For sync messages waiting for response

        PendingMessage() = default;
        PendingMessage(Message msg) : message(std::move(msg)) {}
    };

    // ============================================================================
    // Threaded HSM - State Machine running in its own thread
    // ============================================================================

    /**
     * @brief Threaded HSM with command support and JSON messaging
     *
     * The HSM runs in a dedicated thread, receiving messages (events/commands)
     * via a thread-safe message queue. Responses are sent back through
     * a separate queue or via futures for synchronous messages.
     *
     * There is no distinction between events and commands - the HSM determines
     * whether a message triggers a state change based on its name and current state.
     */
    class ThreadedHSM
    {
      public:
        ThreadedHSM() : running_(false), nextMessageId_(1), syncMessageInProgress_(false) { std::cout << "=== Threaded Laser Tracker HSM Created ===\n"; }

        ~ThreadedHSM() { stop(); }

        /**
         * @brief Start the HSM thread
         */
        void start()
        {
            if (running_.exchange(true))
            {
                return; // Already running
            }

            workerThread_ = std::thread(&ThreadedHSM::workerLoop, this);
            std::cout << "[ThreadedHSM] Worker thread started\n";
        }

        /**
         * @brief Stop the HSM thread
         */
        void stop()
        {
            if (!running_.exchange(false))
            {
                return; // Already stopped
            }

            messageQueue_.stop();
            if (workerThread_.joinable())
            {
                workerThread_.join();
            }
            std::cout << "[ThreadedHSM] Worker thread stopped\n";
        }

        /**
         * @brief Check if HSM thread is running
         */
        bool isRunning() const { return running_.load(); }

        // --------------------------------------------------------------------
        // Unified Message Sending Interface
        // --------------------------------------------------------------------

        /**
         * @brief Send a message asynchronously (fire and forget)
         * @return Message ID for tracking
         */
        uint64_t sendAsync(const std::string& name, JsonValue params = {}, bool sync = false)
        {
            Message msg;
            msg.id         = nextMessageId_++;
            msg.name       = name;
            msg.params     = std::move(params);
            msg.sync       = sync;
            msg.needsReply = false;
            msg.timeoutMs  = 0; // No timeout for async

            queueMessage(std::move(msg));
            return msg.id;
        }

        /**
         * @brief Send a message and wait for response
         * @param name Message name
         * @param params Message parameters
         * @param sync If true, HSM blocks other messages until this completes
         * @param timeoutMs Timeout in milliseconds (0 = no timeout)
         * @return Response message
         */
        Message send(const std::string& name, JsonValue params = {}, bool sync = false, uint32_t timeoutMs = 30000)
        {
            Message msg;
            msg.id         = nextMessageId_++;
            msg.name       = name;
            msg.params     = std::move(params);
            msg.sync       = sync;
            msg.needsReply = true;
            msg.timeoutMs  = timeoutMs;
            // timestamp is set automatically in constructor

            return sendAndWait(std::move(msg));
        }

        // --------------------------------------------------------------------
        // Convenience methods for Events (state-changing messages)
        // --------------------------------------------------------------------

        uint64_t sendEventAsync(const Event& event)
        {
            return sendAsync(getEventName(event), eventToParams(event), false);
        }

        Message sendEventSync(const Event& event, uint32_t timeoutMs = 5000)
        {
            return send(getEventName(event), eventToParams(event), false, timeoutMs);
        }

        // --------------------------------------------------------------------
        // Convenience methods for Commands (may or may not change state)
        // --------------------------------------------------------------------

        uint64_t sendCommandAsync(const Command& cmd)
        {
            return sendAsync(getCommandName(cmd), commandToParams(cmd), isCommandSync(cmd));
        }

        Message sendCommand(const Command& cmd, uint32_t timeoutMs = 30000)
        {
            return send(getCommandName(cmd), commandToParams(cmd), isCommandSync(cmd), timeoutMs);
        }

        // --------------------------------------------------------------------
        // JSON Message Interface
        // --------------------------------------------------------------------

        /**
         * @brief Send a raw JSON message
         * @return Message ID
         */
        uint64_t sendJsonMessage(const std::string& json)
        {
            Message msg = parseJsonMessage(json);
            if (msg.id == 0)
            {
                msg.id = nextMessageId_++;
            }
            queueMessage(std::move(msg));
            return msg.id;
        }

        /**
         * @brief Get response from async response queue
         */
        std::optional<Message> tryGetResponse() { return responseQueue_.tryPop(); }

        /**
         * @brief Wait for a specific response
         */
        std::optional<Message> waitForResponse(uint64_t messageId, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
        {
            auto deadline = std::chrono::steady_clock::now() + timeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                auto response  = responseQueue_.waitPopFor(remaining);
                if (response && response->id == messageId)
                {
                    return response;
                }
                // Put back if not our response
                if (response)
                {
                    responseQueue_.pushFront(std::move(*response));
                }
            }
            return std::nullopt;
        }

        // --------------------------------------------------------------------
        // State Query Interface (thread-safe)
        // --------------------------------------------------------------------

        /**
         * @brief Get current state name (thread-safe)
         */
        std::string getCurrentStateName() const
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            return hsm_.getCurrentStateName();
        }

        /**
         * @brief Check if in a specific state type
         */
        template <typename S> bool isInState() const
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            return hsm_.isInState<S>();
        }

      private:
        HSM                           hsm_;
        std::thread                   workerThread_;
        std::atomic<bool>             running_;
        std::atomic<uint64_t>         nextMessageId_;
        mutable std::mutex            stateMutex_;

        // Message queues
        ThreadSafeQueue<PendingMessage> messageQueue_;
        ThreadSafeQueue<Message>        responseQueue_;

        // Buffered messages (waiting for sync message to complete)
        std::deque<PendingMessage> messageBuffer_;
        std::atomic<bool>          syncMessageInProgress_;
        std::mutex                 bufferMutex_;

        // Pending sync requests (id -> promise)
        std::unordered_map<uint64_t, std::promise<Message>> pendingPromises_;
        std::mutex                                          promiseMutex_;

        // --------------------------------------------------------------------
        // Worker Thread Loop
        // --------------------------------------------------------------------

        void workerLoop()
        {
            std::cout << "[HSM Thread] Started\n";

            while (running_.load())
            {
                try
                {
                    auto pending = messageQueue_.waitPopFor(std::chrono::milliseconds(100));
                    if (!pending)
                    {
                        continue;
                    }

                    processMessage(std::move(*pending));
                }
                catch (const std::exception& e)
                {
                    std::cerr << "[HSM Thread] Exception: " << e.what() << "\n";
                }
            }

            std::cout << "[HSM Thread] Stopped\n";
        }

        void processMessage(PendingMessage pending)
        {
            const Message& msg = pending.message;

            std::cout << "\n[HSM Thread] Processing: '" << msg.name << "' (id=" << msg.id << ", sync=" << (msg.sync ? "true" : "false") << ", age=" << msg.ageMs()
                      << "ms)\n";

            // Check if message has already timed out
            if (msg.needsReply && msg.isTimedOut())
            {
                std::cout << "[HSM Thread] Message timed out before processing (age=" << msg.ageMs() << "ms, timeout=" << msg.timeoutMs << "ms)\n";
                // The sender has already timed out, no point processing
                // But still need to clean up any pending promise
                {
                    std::lock_guard<std::mutex> lock(promiseMutex_);
                    pendingPromises_.erase(msg.id);
                }
                return;
            }

            // If a sync message is in progress, buffer this message
            if (syncMessageInProgress_.load() && msg.sync)
            {
                std::cout << "[HSM Thread] Sync message in progress, buffering\n";
                std::lock_guard<std::mutex> lock(bufferMutex_);
                messageBuffer_.push_back(std::move(pending));
                return;
            }

            // Mark sync in progress if this is a sync message
            if (msg.sync)
            {
                syncMessageInProgress_.store(true);
            }

            // Process the message - try as event first, then as command
            Message response = processMessageContent(msg);

            // Send response if needed
            if (msg.needsReply)
            {
                // Check if there's a promise waiting
                {
                    std::lock_guard<std::mutex> lock(promiseMutex_);
                    auto                        it = pendingPromises_.find(msg.id);
                    if (it != pendingPromises_.end())
                    {
                        it->second.set_value(response);
                        pendingPromises_.erase(it);
                    }
                    else
                    {
                        // Otherwise put in response queue
                        responseQueue_.push(std::move(response));
                    }
                }
            }

            // If this was a sync message and it completed, process buffered messages
            if (msg.sync)
            {
                syncMessageInProgress_.store(false);
                processBufferedMessages();
            }
        }

        void processBufferedMessages()
        {
            std::deque<PendingMessage> toProcess;
            {
                std::lock_guard<std::mutex> lock(bufferMutex_);
                toProcess.swap(messageBuffer_);
            }

            if (!toProcess.empty())
            {
                std::cout << "[HSM Thread] Processing " << toProcess.size() << " buffered messages\n";
            }

            for (auto& pending : toProcess)
            {
                // Skip timed-out messages in buffer
                if (pending.message.needsReply && pending.message.isTimedOut())
                {
                    std::cout << "[HSM Thread] Skipping timed-out buffered message: " << pending.message.name << " (id=" << pending.message.id << ")\n";
                    std::lock_guard<std::mutex> lock(promiseMutex_);
                    pendingPromises_.erase(pending.message.id);
                    continue;
                }
                processMessage(std::move(pending));
            }
        }

        /**
         * @brief Process a message - determines if it's an event or command
         */
        Message processMessageContent(const Message& msg)
        {
            // First, try to process as an event (state-changing)
            auto event = paramsToEvent(msg.name, msg.params);
            if (event)
            {
                return processEvent(msg, *event);
            }

            // Otherwise, process as a command
            return processCommand(msg);
        }

        // --------------------------------------------------------------------
        // Event Processing
        // --------------------------------------------------------------------

        Message processEvent(const Message& msg, const Event& event)
        {
            // Process the event
            bool handled;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                handled = hsm_.processEvent(event);
            }

            JsonValue result    = JsonValue::Object{};
            result["handled"]   = handled;
            result["state"]     = getCurrentStateName();
            result["stateChanged"] = handled;

            if (!handled)
            {
                return Message::createResponse(msg.id, false, result, "Event not handled in current state");
            }

            return Message::createResponse(msg.id, true, result);
        }

        // --------------------------------------------------------------------
        // Command Processing
        // --------------------------------------------------------------------

        Message processCommand(const Message& msg)
        {
            // Check if command is valid in current state
            std::string currentState = getCurrentStateName();

            // Execute command based on name
            if (msg.name == Commands::Home::name)
            {
                return executeHome(msg, currentState);
            }
            else if (msg.name == Commands::GetPosition::name)
            {
                return executeGetPosition(msg, currentState);
            }
            else if (msg.name == Commands::SetLaserPower::name)
            {
                return executeSetLaserPower(msg, currentState);
            }
            else if (msg.name == Commands::Compensate::name)
            {
                return executeCompensate(msg, currentState);
            }
            else if (msg.name == Commands::GetStatus::name)
            {
                return executeGetStatus(msg, currentState);
            }
            else if (msg.name == Commands::MoveRelative::name)
            {
                return executeMoveRelative(msg, currentState);
            }
            else
            {
                return Message::createResponse(msg.id, false, {}, "Unknown message: " + msg.name);
            }
        }

        // --------------------------------------------------------------------
        // Command Executors
        // --------------------------------------------------------------------

        Message executeHome(const Message& msg, const std::string& currentState)
        {
            // Home is only valid in Idle state
            if (currentState.find("Idle") == std::string::npos)
            {
                return Message::createResponse(msg.id, false, {}, "Home command only valid in Idle state (current: " + currentState + ")");
            }

            double speed = 100.0;
            if (msg.params.contains("speed"))
            {
                speed = msg.params.at("speed").asDouble();
            }

            std::cout << "  [COMMAND] Home: Moving to home position at " << speed << "% speed\n";

            // Simulate homing operation (sync)
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(1000 / (speed / 100.0))));

            std::cout << "  [COMMAND] Home: Homing complete\n";

            JsonValue result    = JsonValue::Object{};
            result["position"]  = JsonValue::Object{};
            result["position"]["azimuth"]   = 0.0;
            result["position"]["elevation"] = 0.0;
            result["state"] = getCurrentStateName();

            return Message::createResponse(msg.id, true, result);
        }

        Message executeGetPosition(const Message& msg, const std::string& currentState)
        {
            // GetPosition valid in Idle, Locked, Measuring
            if (currentState.find("Off") != std::string::npos || currentState.find("Initializing") != std::string::npos ||
                currentState.find("Error") != std::string::npos)
            {
                return Message::createResponse(msg.id, false, {}, "GetPosition not available in " + currentState);
            }

            // Simulate reading position
            JsonValue result    = JsonValue::Object{};
            result["position"]  = JsonValue::Object{};
            result["position"]["x"] = 1234.567;
            result["position"]["y"] = 2345.678;
            result["position"]["z"] = 345.789;
            result["position"]["azimuth"]   = 45.123;
            result["position"]["elevation"] = 12.456;

            std::cout << "  [COMMAND] GetPosition: Returned current position\n";

            return Message::createResponse(msg.id, true, result);
        }

        Message executeSetLaserPower(const Message& msg, const std::string& currentState)
        {
            // SetLaserPower valid in any Operational state
            if (currentState.find("Off") != std::string::npos)
            {
                return Message::createResponse(msg.id, false, {}, "SetLaserPower not available when powered off");
            }

            double power = 1.0;
            if (msg.params.contains("powerLevel"))
            {
                power = msg.params.at("powerLevel").asDouble();
            }

            if (power < 0.0 || power > 1.0)
            {
                return Message::createResponse(msg.id, false, {}, "Power level must be between 0.0 and 1.0");
            }

            std::cout << "  [COMMAND] SetLaserPower: Set to " << (power * 100) << "%\n";

            JsonValue result     = JsonValue::Object{};
            result["powerLevel"] = power;

            return Message::createResponse(msg.id, true, result);
        }

        Message executeCompensate(const Message& msg, const std::string& currentState)
        {
            // Compensate valid in Idle, Locked
            if (currentState.find("Idle") == std::string::npos && currentState.find("Locked") == std::string::npos)
            {
                return Message::createResponse(msg.id, false, {}, "Compensate only valid in Idle or Locked state");
            }

            double temp     = 20.0;
            double pressure = 1013.25;
            double humidity = 50.0;

            if (msg.params.contains("temperature"))
                temp = msg.params.at("temperature").asDouble();
            if (msg.params.contains("pressure"))
                pressure = msg.params.at("pressure").asDouble();
            if (msg.params.contains("humidity"))
                humidity = msg.params.at("humidity").asDouble();

            std::cout << "  [COMMAND] Compensate: Applying environmental compensation\n";
            std::cout << "            T=" << temp << "C, P=" << pressure << "hPa, H=" << humidity << "%\n";

            // Simulate calculation (sync)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Calculate compensation factor (simplified)
            double factor = 1.0 + ((temp - 20.0) * 0.000001) + ((pressure - 1013.25) * 0.0000001);

            std::cout << "  [COMMAND] Compensate: Factor = " << std::fixed << std::setprecision(8) << factor << "\n";

            JsonValue result             = JsonValue::Object{};
            result["compensationFactor"] = factor;
            result["applied"]            = true;

            return Message::createResponse(msg.id, true, result);
        }

        Message executeGetStatus(const Message& msg, const std::string& currentState)
        {
            JsonValue result    = JsonValue::Object{};
            result["state"]     = currentState;
            result["healthy"]   = (currentState.find("Error") == std::string::npos);
            result["powered"]   = (currentState.find("Off") == std::string::npos);

            std::cout << "  [COMMAND] GetStatus: State=" << currentState << "\n";

            return Message::createResponse(msg.id, true, result);
        }

        Message executeMoveRelative(const Message& msg, const std::string& currentState)
        {
            // MoveRelative valid in Idle, Locked
            if (currentState.find("Idle") == std::string::npos && currentState.find("Locked") == std::string::npos)
            {
                return Message::createResponse(msg.id, false, {}, "MoveRelative only valid in Idle or Locked state");
            }

            double azimuth   = 0.0;
            double elevation = 0.0;

            if (msg.params.contains("azimuth"))
                azimuth = msg.params.at("azimuth").asDouble();
            if (msg.params.contains("elevation"))
                elevation = msg.params.at("elevation").asDouble();

            std::cout << "  [COMMAND] MoveRelative: Moving by az=" << azimuth << ", el=" << elevation << "\n";

            // Simulate move (sync)
            double moveTime = std::sqrt(azimuth * azimuth + elevation * elevation) * 10;
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(moveTime)));

            std::cout << "  [COMMAND] MoveRelative: Move complete\n";

            JsonValue result      = JsonValue::Object{};
            result["movedAz"]     = azimuth;
            result["movedEl"]     = elevation;
            result["moveTimeMs"]  = static_cast<int>(moveTime);

            return Message::createResponse(msg.id, true, result);
        }

        // --------------------------------------------------------------------
        // Helper Methods
        // --------------------------------------------------------------------

        void queueMessage(Message msg)
        {
            PendingMessage pending;
            pending.message = std::move(msg);
            messageQueue_.push(std::move(pending));
        }

        /**
         * @brief Send a message and wait for response using the message's timeout
         */
        Message sendAndWait(Message msg)
        {
            std::promise<Message> promise;
            auto                  future = promise.get_future();

            uint64_t id        = msg.id;
            uint32_t timeoutMs = msg.timeoutMs;

            {
                std::lock_guard<std::mutex> lock(promiseMutex_);
                pendingPromises_[id] = std::move(promise);
            }

            PendingMessage pending;
            pending.message = std::move(msg);
            messageQueue_.push(std::move(pending));

            // Use the message's remaining time for waiting
            std::future_status status;
            if (timeoutMs == 0)
            {
                // No timeout - wait indefinitely
                future.wait();
                status = std::future_status::ready;
            }
            else
            {
                status = future.wait_for(std::chrono::milliseconds(timeoutMs));
            }

            if (status == std::future_status::ready)
            {
                return future.get();
            }
            else
            {
                // Timeout - remove promise
                {
                    std::lock_guard<std::mutex> lock(promiseMutex_);
                    pendingPromises_.erase(id);
                }
                return Message::createTimeoutResponse(id);
            }
        }

        // --------------------------------------------------------------------
        // Conversion Helpers
        // --------------------------------------------------------------------

        static std::string getCommandName(const Command& cmd)
        {
            return std::visit([](const auto& c) { return std::string(c.getName()); }, cmd);
        }

        static bool isCommandSync(const Command& cmd)
        {
            return std::visit([](const auto& c) { return c.sync; }, cmd);
        }

        static JsonValue eventToParams(const Event& event)
        {
            return std::visit(
                [](const auto& e) -> JsonValue
                {
                    using E = std::decay_t<decltype(e)>;
                    JsonValue params = JsonValue::Object{};

                    if constexpr (std::is_same_v<E, Events::TargetFound>)
                    {
                        params["distance_mm"] = e.distance_mm;
                    }
                    else if constexpr (std::is_same_v<E, Events::InitFailed>)
                    {
                        params["errorReason"] = e.errorReason;
                    }
                    else if constexpr (std::is_same_v<E, Events::MeasurementComplete>)
                    {
                        params["x"] = e.x;
                        params["y"] = e.y;
                        params["z"] = e.z;
                    }
                    else if constexpr (std::is_same_v<E, Events::ErrorOccurred>)
                    {
                        params["errorCode"]   = e.errorCode;
                        params["description"] = e.description;
                    }

                    return params;
                },
                event);
        }

        static JsonValue commandToParams(const Command& cmd)
        {
            return std::visit(
                [](const auto& c) -> JsonValue
                {
                    using C = std::decay_t<decltype(c)>;
                    JsonValue params = JsonValue::Object{};

                    if constexpr (std::is_same_v<C, Commands::Home>)
                    {
                        params["speed"] = c.speed;
                    }
                    else if constexpr (std::is_same_v<C, Commands::SetLaserPower>)
                    {
                        params["powerLevel"] = c.powerLevel;
                    }
                    else if constexpr (std::is_same_v<C, Commands::Compensate>)
                    {
                        params["temperature"] = c.temperature;
                        params["pressure"]    = c.pressure;
                        params["humidity"]    = c.humidity;
                    }
                    else if constexpr (std::is_same_v<C, Commands::MoveRelative>)
                    {
                        params["azimuth"]   = c.azimuth;
                        params["elevation"] = c.elevation;
                    }

                    return params;
                },
                cmd);
        }

        static std::optional<Event> paramsToEvent(const std::string& name, const JsonValue& params)
        {
            if (name == "PowerOn" || name.find("PowerOn") != std::string::npos)
            {
                return Events::PowerOn{};
            }
            else if (name == "PowerOff" || name.find("PowerOff") != std::string::npos)
            {
                return Events::PowerOff{};
            }
            else if (name == "InitComplete" || name.find("InitComplete") != std::string::npos)
            {
                return Events::InitComplete{};
            }
            else if (name == "InitFailed" || name.find("InitFailed") != std::string::npos)
            {
                Events::InitFailed e;
                if (params.contains("errorReason"))
                {
                    e.errorReason = params.at("errorReason").asString();
                }
                return e;
            }
            else if (name == "StartSearch" || name.find("StartSearch") != std::string::npos)
            {
                return Events::StartSearch{};
            }
            else if (name == "TargetFound" || name.find("TargetFound") != std::string::npos)
            {
                Events::TargetFound e;
                if (params.contains("distance_mm"))
                {
                    e.distance_mm = params.at("distance_mm").asDouble();
                }
                return e;
            }
            else if (name == "TargetLost" || name.find("TargetLost") != std::string::npos)
            {
                return Events::TargetLost{};
            }
            else if (name == "StartMeasure" || name.find("StartMeasure") != std::string::npos)
            {
                return Events::StartMeasure{};
            }
            else if (name == "StopMeasure" || name.find("StopMeasure") != std::string::npos)
            {
                return Events::StopMeasure{};
            }
            else if (name == "MeasurementComplete" || name.find("MeasurementComplete") != std::string::npos)
            {
                Events::MeasurementComplete e;
                if (params.contains("x"))
                    e.x = params.at("x").asDouble();
                if (params.contains("y"))
                    e.y = params.at("y").asDouble();
                if (params.contains("z"))
                    e.z = params.at("z").asDouble();
                return e;
            }
            else if (name == "ErrorOccurred" || name.find("Error") != std::string::npos)
            {
                Events::ErrorOccurred e;
                if (params.contains("errorCode"))
                    e.errorCode = params.at("errorCode").asInt();
                if (params.contains("description"))
                    e.description = params.at("description").asString();
                return e;
            }
            else if (name == "Reset" || name.find("Reset") != std::string::npos)
            {
                return Events::Reset{};
            }
            else if (name == "ReturnToIdle" || name.find("ReturnToIdle") != std::string::npos)
            {
                return Events::ReturnToIdle{};
            }

            return std::nullopt;
        }

        static Message parseJsonMessage(const std::string& json)
        {
            // Simple JSON parsing (in production, use a proper JSON library)
            Message msg;

            // Helper to extract integer value
            auto extractInt = [&json](const std::string& key) -> std::optional<int64_t>
            {
                auto pos = json.find("\"" + key + "\"");
                if (pos == std::string::npos)
                    return std::nullopt;
                auto colonPos = json.find(":", pos);
                auto endPos   = json.find_first_of(",}", colonPos);
                if (colonPos == std::string::npos || endPos == std::string::npos)
                    return std::nullopt;
                std::string valStr = json.substr(colonPos + 1, endPos - colonPos - 1);
                valStr.erase(0, valStr.find_first_not_of(" \t\n\r"));
                valStr.erase(valStr.find_last_not_of(" \t\n\r") + 1);
                try
                {
                    return std::stoll(valStr);
                }
                catch (...)
                {
                    return std::nullopt;
                }
            };

            // Helper to extract bool value
            auto extractBool = [&json](const std::string& key) -> std::optional<bool>
            {
                auto pos = json.find("\"" + key + "\"");
                if (pos == std::string::npos)
                    return std::nullopt;
                auto colonPos = json.find(":", pos);
                if (colonPos == std::string::npos)
                    return std::nullopt;
                // Look for true/false after the colon
                auto truePos  = json.find("true", colonPos);
                auto falsePos = json.find("false", colonPos);
                auto nextKey  = json.find("\"", colonPos + 1);
                if (truePos != std::string::npos && (nextKey == std::string::npos || truePos < nextKey))
                    return true;
                if (falsePos != std::string::npos && (nextKey == std::string::npos || falsePos < nextKey))
                    return false;
                return std::nullopt;
            };

            // Extract id
            if (auto id = extractInt("id"))
            {
                msg.id = static_cast<uint64_t>(*id);
            }

            // Extract name
            auto namePos = json.find("\"name\"");
            if (namePos != std::string::npos)
            {
                auto quoteStart = json.find("\"", namePos + 6);
                auto quoteEnd   = json.find("\"", quoteStart + 1);
                if (quoteStart != std::string::npos && quoteEnd != std::string::npos)
                {
                    msg.name = json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                }
            }

            // Extract sync
            if (auto sync = extractBool("sync"))
            {
                msg.sync = *sync;
            }

            // Extract needsReply (default to sync value if not specified)
            if (auto needsReply = extractBool("needsReply"))
            {
                msg.needsReply = *needsReply;
            }
            else
            {
                msg.needsReply = msg.sync;
            }

            // Extract timeoutMs
            if (auto timeout = extractInt("timeoutMs"))
            {
                msg.timeoutMs = static_cast<uint32_t>(*timeout);
            }

            return msg;
        }
    };

} // namespace LaserTracker
