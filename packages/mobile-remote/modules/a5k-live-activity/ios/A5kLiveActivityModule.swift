import ActivityKit
import ExpoModulesCore
import Foundation

/// JS-facing record mirroring A5kSessionAttributes.ContentState.
struct SessionState: Record {
  @Field var link: String = "connecting"
  @Field var tagged: Int = 0
  @Field var untagged: Int = 0
  @Field var battery: Int = -1
  @Field var pending: Int = 0
  @Field var accuracy: Double = -1
  /// Epoch ms, or nil.
  @Field var lastShot: Double? = nil
  @Field var message: String = ""
  /// Seconds after which the system dims the activity as stale if no update comes.
  @Field var staleAfter: Double = 180

  func content() -> ActivityContent<A5kSessionAttributes.ContentState> {
    let state = A5kSessionAttributes.ContentState(
      link: link,
      tagged: tagged,
      untagged: untagged,
      battery: battery,
      pending: pending,
      accuracy: accuracy,
      lastShot: lastShot.map { Date(timeIntervalSince1970: $0 / 1000) },
      message: message
    )
    return ActivityContent(state: state, staleDate: Date().addingTimeInterval(staleAfter))
  }
}

/// Starts, updates and ends the camera-session Live Activity (lock screen + Dynamic Island).
/// Local updates only (no push token): the app is alive in the background through its
/// location background mode whenever there is something to show.
public class A5kLiveActivityModule: Module {
  private func current() -> Activity<A5kSessionAttributes>? {
    Activity<A5kSessionAttributes>.activities.first { $0.activityState == .active || $0.activityState == .stale }
  }

  public func definition() -> ModuleDefinition {
    Name("A5kLiveActivity")

    Function("isEnabled") { () -> Bool in
      ActivityAuthorizationInfo().areActivitiesEnabled
    }

    Function("isRunning") { () -> Bool in
      self.current() != nil
    }

    /// Starts one (or updates the one already running). Returns its id.
    AsyncFunction("start") { (camera: String, state: SessionState) async throws -> String in
      if let running = self.current() {
        await running.update(state.content())
        return running.id
      }
      guard ActivityAuthorizationInfo().areActivitiesEnabled else {
        throw Exception(name: "LiveActivitiesDisabled", description: "Live Activities are turned off for this app")
      }
      let activity = try Activity.request(
        attributes: A5kSessionAttributes(camera: camera),
        content: state.content(),
        pushType: nil
      )
      return activity.id
    }

    AsyncFunction("update") { (state: SessionState) async -> Bool in
      guard let running = self.current() else { return false }
      await running.update(state.content())
      return true
    }

    /// Ends every session activity; with `linger` the final state stays on the lock screen a while.
    AsyncFunction("end") { (state: SessionState?, linger: Bool) async in
      for activity in Activity<A5kSessionAttributes>.activities {
        await activity.end(state?.content(), dismissalPolicy: linger ? .default : .immediate)
      }
    }
  }
}
