/**
 * iOS system dark palette (grouped backgrounds, labels, separators, system tints) so screens sit
 * naturally next to Apple's own apps. The app is dark-only, like a camera viewfinder.
 */
export const colors = {
  /** systemBackground / systemGroupedBackground (dark) */
  background: '#000000',
  /** secondarySystemGroupedBackground: cards, rows */
  panel: '#1C1C1E',
  /** tertiarySystemGroupedBackground: controls on a card (chips, steppers, fields) */
  raised: '#2C2C2E',
  /** opaqueSeparator */
  panelBorder: '#38383A',
  /** separator */
  hairline: 'rgba(84,84,88,0.6)',
  /** over the live view */
  overlay: 'rgba(0,0,0,0.45)',
  /** label */
  text: '#FFFFFF',
  /** secondaryLabel */
  textDim: 'rgba(235,235,245,0.6)',
  /** tertiaryLabel */
  textFaint: 'rgba(235,235,245,0.3)',
  /** systemOrange (dark) — close to the camera's own orange */
  accent: '#FF9F0A',
  /** systemRed / systemGreen (dark) */
  danger: '#FF453A',
  ok: '#30D158',
};

/** Card corners: continuous (squircle) like iOS. */
export const radius = { card: 16, row: 12, control: 10 } as const;
