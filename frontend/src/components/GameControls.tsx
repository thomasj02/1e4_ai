'use client';

import React from 'react';
import { Form } from 'react-bootstrap';
import type { GameControlsProps } from '@/types';

const GameControls: React.FC<GameControlsProps> = ({
  onOpenFENDialog,
  onNewGame,
  onExportPGN,
  onResign,
  gameInProgress,
  soundEnabled,
  onSoundEnabledChange
}) => {
  return (
    <div className="vstack gap-2">

      <div className="d-flex gap-3 justify-content-center flex-wrap">
        <button onClick={onNewGame} className="btn btn-primary">New Game</button>
        <button onClick={onOpenFENDialog} className="btn btn-primary">FEN</button>
        <button onClick={onExportPGN} className="btn btn-primary">Export PGN</button>
        <button
          onClick={onResign}
          className="btn btn-outline-danger"
          disabled={!gameInProgress}
          title="Resign"
        >
          <i className="bi bi-flag-fill me-1"></i>
          Resign
        </button>
      </div>

      {/* Sound settings */}
      {soundEnabled !== undefined && onSoundEnabledChange && (
        <div className="text-center">
          <Form.Check
            type="checkbox"
            id="sound-warnings"
            label="🔊 Sound warnings"
            checked={soundEnabled}
            onChange={(e) => onSoundEnabledChange(e.target.checked)}
            className="d-inline-flex align-items-center gap-2"
          />
        </div>
      )}
    </div>
  );
};

export default GameControls;