/**
 * Chess application constants
 */

export const BACKEND_URL: string = process.env.NEXT_PUBLIC_API_URL || 'http://localhost:8000';

export const CSS_VARIABLE_FALLBACKS = {
  '--color-square-last-move-pale': 'rgba(255, 255, 190, 0.5)',
  '--color-square-last-move': 'rgba(255, 255, 190, 0.7)',
  '--color-square-selected': 'rgba(255, 255, 0, 0.2)',
  '--color-square-navigation': 'rgba(255, 255, 0, 0.4)',
  '--color-chess-legal': 'rgba(0, 128, 0, 0.4)'
} as const;

export type CSSVariableName = keyof typeof CSS_VARIABLE_FALLBACKS;