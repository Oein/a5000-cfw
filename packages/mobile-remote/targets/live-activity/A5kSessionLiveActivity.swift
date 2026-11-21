import ActivityKit
import SwiftUI
import WidgetKit

private let accent = Color(red: 0.953, green: 0.596, blue: 0.0)

private extension A5kSessionAttributes.ContentState {
  var linkLabel: String {
    switch link {
    case "linked": return "연결됨"
    case "connecting": return "연결 중"
    case "lost": return "끊김"
    default: return "대기"
    }
  }
  var linkColor: Color {
    switch link {
    case "linked": return .green
    case "connecting": return .yellow
    case "lost": return .red
    default: return .gray
    }
  }
  var gpsText: String { accuracy < 0 ? "GPS 없음" : "±\(Int(accuracy.rounded()))m" }
  var batteryText: String { battery < 0 ? "–" : "\(battery)%" }
  var batterySymbol: String {
    switch battery {
    case ..<0: return "battery.0percent"
    case ..<25: return "battery.25percent"
    case ..<50: return "battery.50percent"
    case ..<75: return "battery.75percent"
    default: return "battery.100percent"
    }
  }
}

/// Lock screen / banner layout.
private struct LockScreenView: View {
  let context: ActivityViewContext<A5kSessionAttributes>

  var body: some View {
    let s = context.state
    VStack(alignment: .leading, spacing: 8) {
      HStack {
        Image(systemName: "camera.fill").foregroundStyle(accent)
        Text(context.attributes.camera).font(.headline)
        Circle().fill(s.linkColor).frame(width: 8, height: 8)
        Text(s.linkLabel).font(.subheadline).foregroundStyle(.secondary)
        Spacer()
        Image(systemName: s.batterySymbol)
        Text(s.batteryText).font(.subheadline.monospacedDigit())
      }
      HStack(spacing: 16) {
        Label("\(s.tagged)", systemImage: "location.fill").foregroundStyle(accent)
        if s.untagged > 0 {
          Label("\(s.untagged)", systemImage: "location.slash").foregroundStyle(.secondary)
        }
        Label(s.gpsText, systemImage: "scope").foregroundStyle(.secondary)
        if s.pending > 0 {
          Label("\(s.pending)", systemImage: "arrow.down.circle").foregroundStyle(.secondary)
        }
        Spacer()
        if let last = s.lastShot {
          Text(last, style: .relative).font(.caption.monospacedDigit()).foregroundStyle(.secondary)
        }
      }
      .font(.subheadline.monospacedDigit())
      if !s.message.isEmpty {
        Text(s.message).font(.caption).foregroundStyle(.secondary).lineLimit(1)
      }
    }
    .padding(16)
    .activityBackgroundTint(Color.black.opacity(0.85))
    .activitySystemActionForegroundColor(accent)
  }
}

struct A5kSessionLiveActivity: Widget {
  var body: some WidgetConfiguration {
    ActivityConfiguration(for: A5kSessionAttributes.self) { context in
      LockScreenView(context: context)
    } dynamicIsland: { context in
      let s = context.state
      return DynamicIsland {
        DynamicIslandExpandedRegion(.leading) {
          HStack(spacing: 6) {
            Image(systemName: "camera.fill").foregroundStyle(accent)
            Circle().fill(s.linkColor).frame(width: 7, height: 7)
            Text(s.linkLabel).font(.caption)
          }
        }
        DynamicIslandExpandedRegion(.trailing) {
          HStack(spacing: 4) {
            Image(systemName: s.batterySymbol)
            Text(s.batteryText).font(.caption.monospacedDigit())
          }
        }
        DynamicIslandExpandedRegion(.bottom) {
          HStack(spacing: 14) {
            Label("\(s.tagged)장 위치 기록", systemImage: "location.fill").foregroundStyle(accent)
            Label(s.gpsText, systemImage: "scope")
            if s.pending > 0 { Label("\(s.pending)", systemImage: "arrow.down.circle") }
            Spacer()
            if let last = s.lastShot { Text(last, style: .relative).monospacedDigit() }
          }
          .font(.caption)
        }
      } compactLeading: {
        Image(systemName: s.link == "linked" ? "camera.fill" : "camera").foregroundStyle(s.link == "linked" ? accent : .secondary)
      } compactTrailing: {
        HStack(spacing: 2) {
          Image(systemName: "location.fill").font(.caption2)
          Text("\(s.tagged)").font(.caption.monospacedDigit())
        }
        .foregroundStyle(accent)
      } minimal: {
        Image(systemName: "camera.fill").foregroundStyle(s.link == "linked" ? accent : .secondary)
      }
      .keylineTint(accent)
    }
  }
}
