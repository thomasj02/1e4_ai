'use client';

import React from 'react';
import type { GameStatusProps } from '../types';

const GameStatus: React.FC<GameStatusProps> = ({ game, gameEndedByTime, timeWinner, gameEndedByResignation, resignedPlayer }) => {
  const isCheckmate = game.isCheckmate();
  const isStalemate = game.isStalemate();
  const isInsufficientMaterial = game.isInsufficientMaterial();
  const isThreefoldRepetition = game.isThreefoldRepetition();
  const isDraw = game.isDraw() || isStalemate || isInsufficientMaterial || isThreefoldRepetition;
  const isCheck = game.isCheck();
  const currentTurn = game.turn();

  // Determine what message to show
  let statusMessage = '';
  let showGameOver = false;

  if (gameEndedByResignation && resignedPlayer) {
    const winner = resignedPlayer === 'white' ? 'Black' : 'White';
    statusMessage = `${winner} wins by resignation!`;
    showGameOver = true;
  } else if (gameEndedByTime && timeWinner) {
    statusMessage = `${timeWinner === 'white' ? 'White' : 'Black'} wins on time!`;
    showGameOver = true;
  } else if (isCheckmate) {
    statusMessage = `Checkmate! ${currentTurn === 'w' ? 'Black' : 'White'} wins!`;
    showGameOver = true;
  } else if (isDraw) {
    if (isStalemate) {
      statusMessage = 'Draw!';
    } else if (isInsufficientMaterial) {
      statusMessage = 'Draw!';
    } else if (isThreefoldRepetition) {
      statusMessage = 'Draw!';
    } else {
      statusMessage = 'Draw!';
    }
    showGameOver = true;
  } else if (isCheck) {
    statusMessage = 'Check!';
  }
  
  return (
    <div className="d-flex flex-column justify-content-center align-items-center gap-2">
      {showGameOver && (
        <div className="alert alert-danger">
          <span>Game Over!</span>
        </div>
      )}
      
      {statusMessage && (
        <div className={showGameOver ? "alert alert-danger" : "alert alert-warning"}>
          <span>{statusMessage}</span>
        </div>
      )}
    </div>
  );
};

export default GameStatus;