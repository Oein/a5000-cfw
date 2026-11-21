// KEEP IN SYNC with targets/live-activity/A5kSessionAttributes.swift — ActivityKit matches the
// app's and the widget extension's copies by type name and Codable layout.
import ActivityKit
import Foundation

public struct A5kSessionAttributes: ActivityAttributes {
  public struct ContentState: Codable, Hashable {
    /// "linked" | "connecting" | "lost" | "standby"
    public var link: String
    /// Photos the camera geotagged with the phone's position this session.
    public var tagged: Int
    /// GPS requests the phone couldn't answer (no fix).
    public var untagged: Int
    /// Camera battery 0...100, -1 unknown.
    public var battery: Int
    /// Photos on the camera not yet copied to the phone.
    public var pending: Int
    /// Horizontal accuracy of the phone's latest fix in metres, -1 none.
    public var accuracy: Double
    /// Last shot (geotag answer) time, nil before the first.
    public var lastShot: Date?
    /// One short line, e.g. "GPS 응답 ±8m".
    public var message: String
  }

  /// Camera model, e.g. "ILCE-5000".
  public var camera: String
}
