import { formatClockTime } from './chess';
import type { PGNGenerationParams } from '../types';

/**
 * Generate PGN with clock annotations
 */
export const generatePGNWithClocks = ({
  game,
  moveHistory,
  moveTimes,
  playerColor,
  botRating,
  timeControl,
  gameEndedByTime,
  timeWinner
}: PGNGenerationParams): string => {
  // Get base PGN from chess.js
  const basePGN = game.pgn();
  
  // Create metadata headers
  const date = new Date();
  const dateStr = `${date.getFullYear()}.${(date.getMonth() + 1).toString().padStart(2, '0')}.${date.getDate().toString().padStart(2, '0')}`;
  
  const headers = [
    `[Event "ChessMimic Game"]`,
    `[Site "ChessMimic"]`,
    `[Date "${dateStr}"]`,
    `[Round "1"]`,
    `[White "${playerColor === 'white' ? 'Human' : `ChessMimic Bot (${botRating})`}"]`,
    `[Black "${playerColor === 'black' ? 'Human' : `ChessMimic Bot (${botRating})`}"]`,
    `[TimeControl "${Math.floor(timeControl.initial / 1000)}+${Math.floor(timeControl.increment / 1000)}"]`
  ];
  
  // Add custom FEN if game started from non-standard position
  const startingFEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
  if (moveHistory.length > 0 && (moveHistory as typeof moveHistory & { customStartFen?: string }).customStartFen) {
    headers.push(`[FEN "${(moveHistory as typeof moveHistory & { customStartFen?: string }).customStartFen}"]`);
    headers.push(`[SetUp "1"]`);
  } else {
    // Try to check if game has custom starting position
    try {
      const pgn = game.pgn();
      const fenMatch = pgn.match(/\[FEN "([^"]+)"\]/);
      if (fenMatch && fenMatch[1] && fenMatch[1] !== startingFEN) {
        headers.push(`[FEN "${fenMatch[1]}"]`);
        headers.push(`[SetUp "1"]`);
      }
    } catch {
      // Ignore errors
    }
  }
  
  // Add termination if game ended by time
  if (gameEndedByTime) {
    headers.push(`[Termination "Time forfeit"]`);
  } else if (game.isCheckmate()) {
    headers.push(`[Termination "Normal"]`);
  }
  
  // Add result
  let result = '*';
  if (game.isCheckmate()) {
    result = game.turn() === 'w' ? '0-1' : '1-0';
  } else if (game.isDraw()) {
    result = '1/2-1/2';
  } else if (gameEndedByTime) {
    result = timeWinner === 'white' ? '1-0' : '0-1';
  }
  headers.push(`[Result "${result}"]`);
  
  // Parse the moves and add clock annotations
  if (moveHistory.length > 0 && moveTimes.white.length > 0) {
    // Build moves with clock annotations
    let annotatedMoves = '';
    let moveNumber = 1;
    
    for (let i = 0; i < moveHistory.length; i++) {
      const move = moveHistory[i];
      const isWhiteMove = move.color === 'w';
      
      if (isWhiteMove) {
        annotatedMoves += `${moveNumber}. `;
      }
      
      annotatedMoves += move.san;
      
      // Add clock annotation
      const moveTimeIndex = Math.floor(i / 2);
      const clockTimes = isWhiteMove ? moveTimes.white : moveTimes.black;
      if (clockTimes[moveTimeIndex] !== undefined) {
        annotatedMoves += ` {[%clk ${formatClockTime(clockTimes[moveTimeIndex])}]}`;
      }
      
      if (isWhiteMove && i === moveHistory.length - 1) {
        // Last move was white, no black response yet
        annotatedMoves += ' ';
      } else if (!isWhiteMove) {
        annotatedMoves += ' ';
        moveNumber++;
      } else {
        annotatedMoves += ' ';
      }
    }
    
    // Add result
    annotatedMoves += result;
    
    return headers.join('\n') + '\n\n' + annotatedMoves.trim();
  } else {
    // No moves with clocks, return basic PGN
    return headers.join('\n') + '\n\n' + (basePGN.includes(result) ? basePGN : basePGN + ' ' + result);
  }
};

/**
 * Export PGN content as a downloadable file
 */
export const handleExportPGN = (pgnContent: string): void => {
  // Create blob and download
  const blob = new Blob([pgnContent], { type: 'application/x-chess-pgn' });
  const url = URL.createObjectURL(blob);
  
  // Generate filename with timestamp
  const date = new Date();
  const timestamp = `${date.getFullYear()}-${(date.getMonth() + 1).toString().padStart(2, '0')}-${date.getDate().toString().padStart(2, '0')}_${date.getHours().toString().padStart(2, '0')}-${date.getMinutes().toString().padStart(2, '0')}`;
  const filename = `chessmimic_game_${timestamp}.pgn`;
  
  // Create download link
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.style.display = 'none';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  
  // Clean up
  URL.revokeObjectURL(url);
};