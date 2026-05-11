'use client';

import React, {useCallback, useEffect, useMemo, useState} from 'react';
import {Chess, type Square} from 'chess.js';
import {authenticatedFetch, useAuth} from '@utils/auth';
import {SignInButton, SignUpButton, SignedIn, SignedOut} from '@clerk/nextjs';
import CustomUserButton from '../components/CustomUserButton';
import Link from 'next/link';
import {useChessClock} from '@hooks/useChessClock';
import {useChessGame} from '@hooks/useChessGame';
import {usePostHog} from 'posthog-js/react';
import NewGameDialog from '../components/NewGameDialog';
import GameStatus from '../components/GameStatus';
import GameControls from '../components/GameControls';
import ChessboardArea from '../components/ChessboardArea';
import MoveHistory from '../components/MoveHistory';
import PlayerInfoPanel from '../components/PlayerInfoPanel';
import TimeEditDialog from '../components/TimeEditDialog';
import FENDialog from '../components/FENDialog';
import RatingChangeModal from '../components/RatingChangeModal';
import {useUserRating} from '@/hooks/useUserRating';
import type {BotMoveResponse, ChessJsMove, EnhancedHistoryEntry, EvalState, GameSettings, TimeControl, UiState} from '@/types';
import type {MoveRecord} from '@/types/rating';
import {BACKEND_URL} from '@constants/chess';
import {generateEnhancedHistory, getAIMoveSAN, getCSSVariable} from '@utils/chess';
import {generatePGNWithClocks, handleExportPGN} from '@utils/pgn';

// Extend window interface for debug state
declare global {
  interface Window {
    __chessAppState?: {
      fen: string;
      turn: 'w' | 'b';
      moveHistory: ChessJsMove[];
      gameState: {
        inCheck: boolean;
        isCheckmate: boolean;
        isStalemate: boolean;
        isDraw: boolean;
        isInsufficientMaterial: boolean;
        isThreefoldRepetition: boolean;
        status: string;
      };
      evalState: EvalState;
      whiteTime: number;
      blackTime: number;
      clockRunning: boolean;
      activeColor: 'white' | 'black' | null;
      uiState: UiState;
      gameSettings: GameSettings;
      viewedPlyIndex: number;
      isViewingLatest: boolean;
    };
    __exportPGN?: () => string;
  }
}

export default function ChessGame() {
  // Auth hook
  const { getToken } = useAuth();
  
  // PostHog analytics hook
  const posthog = usePostHog();

  // User rating hook
  const {
    rating: userRating,
    lastRatingUpdate,
    recordGameResult,
    clearLastRatingUpdate,
  } = useUserRating();

  // Chess game hook
  const {
    game,
    moveHistory,
    viewedPlyIndex,
    moveToPromote,
    legalSquares,
    squareStyles,
    selectedPiece,
    lastMoveSquares,
    isViewingLatest,
    enhancedHistory,
    setGame,
    setMoveHistory,
    setViewedPlyIndex,
    setMoveToPromote,
    setLegalSquares,
    setSquareStyles,
    setSelectedPiece,
    setLastMoveSquares,
    resetGame,
    loadGameFromFEN,
    navigateToPly,
    getViewedFEN
  } = useChessGame();

  // Game settings
  const [gameSettings, setGameSettings] = useState<GameSettings>({
    playerColor: 'white',
    soundEnabled: true,
    botRating: 1800
  });

  // Sound effect handler for 15-second time warning
  const handleTimeWarning = useCallback(() => {
    if (!gameSettings.soundEnabled) return;
    
    try {
      // Create a warning beep sound
      // In a production app, you'd load actual audio files
      const AudioContextConstructor = (window as Window & { AudioContext?: typeof AudioContext; webkitAudioContext?: typeof AudioContext }).AudioContext || 
                                      (window as Window & { AudioContext?: typeof AudioContext; webkitAudioContext?: typeof AudioContext }).webkitAudioContext;
      
      if (!AudioContextConstructor) {
        console.warn('AudioContext not available');
        return;
      }
      
      const audioContext = new AudioContextConstructor();
      const oscillator = audioContext.createOscillator();
      const gainNode = audioContext.createGain();
      
      oscillator.connect(gainNode);
      gainNode.connect(audioContext.destination);
      
      // High pitched warning beep at 1200Hz
      oscillator.frequency.setValueAtTime(1200, audioContext.currentTime);
      oscillator.type = 'sine';
      
      // 0.5 second beep
      gainNode.gain.setValueAtTime(0.3, audioContext.currentTime);
      gainNode.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + 0.5);
      
      oscillator.start(audioContext.currentTime);
      oscillator.stop(audioContext.currentTime + 0.5);
      
      console.log('🔊 Time warning: 15 seconds remaining');
    } catch (error) {
      console.warn('Could not play sound:', error);
    }
  }, [gameSettings.soundEnabled]);

  // Time control settings
  const [timeControl, setTimeControl] = useState<TimeControl>({ initial: 300000, increment: 3000 }); // 5+3

  // Clock hook
  const {
    whiteTime,
    blackTime,
    activeColor,
    clockRunning,
    gameEndedByTime,
    timeWinner,
    moveTimes,
    handleMoveCompletion,
    stopClock,
    resetClock,
    addMoveTime,
    setClockRunning,
    setWhiteTime,
    setBlackTime,
    setActiveColor
  } = useChessClock(timeControl.initial, timeControl.increment);

  // UI State
  const [uiState, setUiState] = useState<UiState>({
    fenInput: '',
    fenError: ''
  });

  // Other state
  const [, setBotThinkingTime] = useState<number | null>(null);
  const [isNewGameDialogOpen, setIsNewGameDialogOpen] = useState(false);
  const [isFENDialogOpen, setIsFENDialogOpen] = useState(false);
  const [timeEditDialog, setTimeEditDialog] = useState<{
    isOpen: boolean;
    player: 'white' | 'black' | null;
  }>({ isOpen: false, player: null });
  const [ratingModalState, setRatingModalState] = useState<{
    isOpen: boolean;
    gameResult: 'win' | 'loss' | 'draw';
    resultReason?: string;
  }>({ isOpen: false, gameResult: 'draw' });
  const [hasRecordedGameResult, setHasRecordedGameResult] = useState(false);
  const [hasSetInitialBotRating, setHasSetInitialBotRating] = useState(false);

  // Move records for storing full game data
  const [moveRecords, setMoveRecords] = useState<MoveRecord[]>([]);

  // Resignation state
  const [gameEndedByResignation, setGameEndedByResignation] = useState(false);
  const [resignedPlayer, setResignedPlayer] = useState<'white' | 'black' | null>(null);

  // Set initial bot rating to user's rating when it first loads
  useEffect(() => {
    if (userRating && !hasSetInitialBotRating) {
      setGameSettings(prev => ({
        ...prev,
        botRating: Math.min(3500, Math.max(400, Math.round(userRating.rating)))
      }));
      setHasSetInitialBotRating(true);
    }
  }, [userRating, hasSetInitialBotRating]);

  // Evaluation bar state
  const [evalState, setEvalState] = useState<EvalState>({
    evaluation: 0,
    loading: false,
    error: null
  });
  const [, setBoardHeight] = useState<number | null>(null);

  // Helper to record move data for game storage
  const recordMoveData = useCallback((sanMove: string, clockTimeMs: number) => {
    const newRecord: MoveRecord = {
      move: sanMove,
      clock: clockTimeMs,
      eval: {
        white: evalState.whiteProb ?? 0,
        draw: evalState.drawProb ?? 0,
        black: evalState.blackProb ?? 0,
      },
    };
    setMoveRecords(prev => [...prev, newRecord]);
  }, [evalState]);

  // PGN Export helpers
  const pgnWithClocks = useMemo(() => {
    return generatePGNWithClocks({
      game,
      moveHistory,
      moveTimes,
      playerColor: gameSettings.playerColor === 'random' ? 'white' : gameSettings.playerColor,
      botRating: gameSettings.botRating,
      timeControl,
      gameEndedByTime,
      timeWinner
    });
  }, [game, moveHistory, moveTimes, gameSettings.playerColor, gameSettings.botRating, timeControl, gameEndedByTime, timeWinner]);

  const exportPGN = useCallback(() => {
    handleExportPGN(pgnWithClocks);
    
    // Track PGN export event in PostHog
    posthog?.capture('pgn_exported', {
      moveCount: game.history().length,
      playerColor: gameSettings.playerColor,
      botRating: gameSettings.botRating,
      timeControl: timeControl ? `${timeControl.initial / 60000}+${timeControl.increment / 1000}` : 'unlimited',
      gameStatus: game.isGameOver() ? 'completed' : 'in_progress',
      fen: game.fen()
    });
  }, [pgnWithClocks, posthog, game, gameSettings.playerColor, gameSettings.botRating, timeControl]);

  

  // Calculate displayed FEN based on viewed position
  const displayedFen = useMemo(() => {
    return getViewedFEN();
  }, [getViewedFEN]);

  // Game state calculations
  const gameState = useMemo(() => {
    const inCheck = game.isCheck();
    const isCheckmate = game.isCheckmate();
    const isStalemate = game.isStalemate();
    const isInsufficientMaterial = game.isInsufficientMaterial();
    const isThreefoldRepetition = game.isThreefoldRepetition();
    const isDraw = isStalemate || isInsufficientMaterial || isThreefoldRepetition || game.isDraw();

    let status = '';
    if (isCheckmate) {
      status = `Checkmate! ${game.turn() === 'w' ? 'Black' : 'White'} wins!`;
    } else if (isDraw) {
      if (isStalemate) {
        status = 'Draw by stalemate';
      } else if (isInsufficientMaterial) {
        status = 'Draw by insufficient material';
      } else if (isThreefoldRepetition) {
        status = 'Draw by threefold repetition';
      } else {
        status = 'Draw';
      }
    }

    return {
      inCheck,
      isCheckmate,
      isStalemate,
      isDraw,
      isInsufficientMaterial,
      isThreefoldRepetition,
      status
    };
  }, [game]);


  // Helper for debug state updates
  const updateDebugState = useCallback(() => {
    if (typeof window !== 'undefined') {
      window.__chessAppState = {
        fen: game.fen(),
        turn: game.turn(),
        moveHistory,
        gameState,
        evalState,
        whiteTime,
        blackTime,
        clockRunning,
        activeColor,
        uiState,
        gameSettings,
        viewedPlyIndex,
        isViewingLatest
      };
    }
  }, [game, moveHistory, gameState, evalState, whiteTime, blackTime, clockRunning, activeColor, uiState, gameSettings, viewedPlyIndex, isViewingLatest]);

  const fetchBotMove = useCallback(async (currentGameInstance: Chess): Promise<BotMoveResponse> => {
    try {
      const response = await authenticatedFetch(`${BACKEND_URL}/get_move`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          fen: currentGameInstance.fen(),
          moves: currentGameInstance.history(),
          rating: gameSettings.botRating,
          clock_time: blackTime / 1000,  // Convert milliseconds to seconds
          min_probability: 0.01  // Default 1% minimum move probability
        })
      }, getToken);
      if (!response.ok) throw new Error('Network response was not ok');
      const data = await response.json();
      // Store thinking time if provided
      if (data.thinking_time !== undefined) {
        setBotThinkingTime(data.thinking_time);
      }
      // Return both move and thinking time
      return { 
        move: data.move || null, 
        thinking_time: data.thinking_time !== undefined ? data.thinking_time : 0 
      };
    } catch (e) {
      console.error('Error fetching move from backend:', e);
      // Set a random thinking time for fallback moves
      const fallbackThinkingTime = Math.random() * 3;
      setBotThinkingTime(fallbackThinkingTime);
      const aiMove = getAIMoveSAN(currentGameInstance);
      if (!aiMove) {
        throw new Error('No valid moves available');
      }
      return { 
        move: aiMove, 
        thinking_time: fallbackThinkingTime 
      };
    }
  }, [gameSettings.botRating, blackTime, setBotThinkingTime, getToken]);

  async function fetchEvaluation(fen: string, moves: string[] = []) {
    setEvalState(prev => ({ ...prev, loading: true, error: null }));
    
    try {
      const response = await authenticatedFetch(`${BACKEND_URL}/evaluate_position`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          fen: fen,
          moves: moves,
          white_rating: gameSettings.botRating,
          black_rating: gameSettings.botRating,
          white_clock: whiteTime / 1000,  // Convert to seconds
          black_clock: blackTime / 1000,  // Convert to seconds
          increment: timeControl.increment / 1000
        })
      }, getToken);
      
      if (!response.ok) {
        throw new Error('Failed to fetch evaluation');
      }
      
      const data = await response.json();
      
      // Check if the response contains an error
      if (data.error) {
        setEvalState(prev => ({ 
          ...prev, 
          error: data.error,
          blackProb: undefined,
          drawProb: undefined,
          whiteProb: undefined
        }));
      } else {
        setEvalState(prev => ({ 
          ...prev, 
          evaluation: data.evaluation,
          blackProb: data.black_prob,
          drawProb: data.draw_prob,
          whiteProb: data.white_prob,
          error: null
        }));
      }
      
    } catch (error: unknown) {
      console.error('Error fetching evaluation:', error);
      setEvalState(prev => ({ 
        ...prev, 
        error: error instanceof Error ? error.message : 'Unknown error',
        blackProb: undefined,
        drawProb: undefined,
        whiteProb: undefined
      }));
    } finally {
      setEvalState(prev => ({ ...prev, loading: false }));
    }
  }

  const applyGameInstance = useCallback((updatedGame: Chess, currentClockTime: number | null = null) => {
    const newHistory = generateEnhancedHistory(updatedGame);
    setLegalSquares([]);
    setSelectedPiece(null);

    const lastMoveIndex = newHistory.length > 0 ? newHistory.length - 1 : -1;
    const moveToHighlight = lastMoveIndex >= 0 ? newHistory[lastMoveIndex] : null;

    if (moveToHighlight && moveToHighlight.from && moveToHighlight.to) {
      setLastMoveSquares({ from: moveToHighlight.from, to: moveToHighlight.to });
      const newSquareStyles: Record<string, React.CSSProperties> = {};
      newSquareStyles[moveToHighlight.from] = { backgroundColor: getCSSVariable('--color-square-last-move') };
      newSquareStyles[moveToHighlight.to] = { backgroundColor: getCSSVariable('--color-square-last-move') };
      setSquareStyles(newSquareStyles);
    } else {
      setLastMoveSquares({ from: null, to: null });
      setSquareStyles({});
    }

    // Track clock time for the move if provided
    if (currentClockTime !== null && lastMoveIndex >= 0 && moveToHighlight) {
      const moveColor = moveToHighlight.color === 'w' ? 'white' : 'black';
      addMoveTime(moveColor, currentClockTime);
    }

    setMoveHistory(newHistory);
    setViewedPlyIndex(newHistory.length - 1);
    setGame(updatedGame);
  }, [setLegalSquares, setSelectedPiece, setLastMoveSquares, setSquareStyles, addMoveTime, setMoveHistory, setViewedPlyIndex, setGame]);

  const handleComputerTurn = useCallback(async (currentGame: Chess) => {
    // Check if game is over, including time forfeit
    if (currentGame.isGameOver() || gameEndedByTime) {
      stopClock();
      return;
    }
    
    // Bot's clock is already running (set in handleMoveCompletion)
    const aiResponse = await fetchBotMove(currentGame);
    
    // Simulate bot thinking time using the value from the response
    if (aiResponse.thinking_time > 0) {
      // Use setTimeout to delay the move execution
      await new Promise<void>(resolve => {
        const thinkingTimeMs = aiResponse.thinking_time * 1000;
        setTimeout(resolve, thinkingTimeMs);
      });
    }
    
    // If no move, game is likely over
    if (!aiResponse.move) {
      setClockRunning(false);
      return;
    }

    // Check if game is still active (in case it ended during thinking)
    if (currentGame.isGameOver() || gameEndedByTime) {
      stopClock();
      return;
    }
    

    const updatedGame = new Chess();
    try {
      updatedGame.loadPgn(currentGame.pgn());
    } catch (error: unknown) {
      console.warn('Could not load PGN:', error instanceof Error ? error.message : 'Unknown error');
      updatedGame.load(currentGame.fen());
    }

    try {
      updatedGame.move(aiResponse.move);
    } catch (e) {
      console.error('Error applying AI move:', e);
      return;
    }

    // Pass the bot's current clock time when applying the move
    const botClockTime = currentGame.turn() === 'w' ? whiteTime : blackTime;
    applyGameInstance(updatedGame, botClockTime);

    // Record move data for game storage
    recordMoveData(aiResponse.move, botClockTime);

    // Clock management for computer move
    // Determine who just moved and who's turn it is now
    // The player who just moved is the opposite of whose turn it is now
    const currentTurn = updatedGame.turn() === 'w' ? 'white' : 'black';
    const playerWhoJustMoved = currentTurn === 'white' ? 'black' : 'white';

    // Use consistent clock management through handleMoveCompletion
    // Bot always gets increment after their move
    handleMoveCompletion(playerWhoJustMoved, currentTurn, false);
    
    // Clear bot thinking time
    setBotThinkingTime(null);
  }, [gameEndedByTime, blackTime, whiteTime, handleMoveCompletion, setClockRunning, stopClock, fetchBotMove, applyGameInstance, setBotThinkingTime, recordMoveData]);

  const handlePromotionCheck = useCallback((sourceSquare: string, targetSquare: string, piece: string): boolean => {
    // First, check if piece is a pawn moving to rank 1 or 8
    const isPotentialPromotion = piece.toLowerCase().endsWith('p') &&
                        (targetSquare.endsWith('1') || targetSquare.endsWith('8'));
    
    // If not a potential promotion, return false immediately
    if (!isPotentialPromotion) {
        setMoveToPromote(null);
        return false;
    }
    
    // Verify the move is legal using chess.js
    // For promotions, we need to specify a promotion piece ('q' by default)
    // because chess.js requires it for validation
    try {
        const validationGame = new Chess(game.fen());
        const moveData = {
            from: sourceSquare,
            to: targetSquare,
            promotion: 'q' // Default to queen for validation
        };
        
        const moveResult = validationGame.move(moveData);
        
        // If the move is legal, set moveToPromote and return true
        if (moveResult) {
            console.log("Legal promotion move detected:", moveResult.san);
            setMoveToPromote({ from: sourceSquare, to: targetSquare, piece });
            return true;
        }
    } catch (e: unknown) {
        console.log("Invalid promotion move:", e instanceof Error ? e.message : 'Unknown error');
    }
    
    // If we got here, it's either not a valid move or some error occurred
    setMoveToPromote(null);
    return false;
  }, [game, setMoveToPromote]);

  // Handles promotion moves with AI response
  function handlePromotionPieceSelect(piece?: string): boolean {
    const promotionPiece = piece ? piece.toLowerCase().charAt(1) : null;
    if (!moveToPromote || !promotionPiece || !isViewingLatest) {
      if (!isViewingLatest) console.log("Cannot make promotion move while browsing history.");
      setMoveToPromote(null); 
      return false;
    }

    if (gameEndedByTime || gameEndedByResignation) {
      console.log("Cannot make promotion move after game has ended.");
      setMoveToPromote(null);
      return false;
    }

    // --- Prepare Moves ---
    // 1. Prepare user's promotion move data
    const userMoveData = {
        from: moveToPromote.from,
        to: moveToPromote.to,
        promotion: promotionPiece
    };

    // --- Create a fresh game with full history preserved ---
    const updatedGame = new Chess();

    // First load the current game's PGN to preserve full history
    try {
      updatedGame.loadPgn(game.pgn());
    } catch(e: unknown) {
      // Fallback to just loading the current position
      updatedGame.load(game.fen());
      console.warn("Couldn't load PGN for promotion, using FEN instead:", e instanceof Error ? e.message : 'Unknown error');
    }

    // --- Apply user's promotion move ---
    let userMoveResult = null;
    try {
      userMoveResult = updatedGame.move(userMoveData);
      console.log(`Promotion move applied: ${userMoveData.from}${userMoveData.to}=${promotionPiece}`, userMoveResult);
      
      // Track promotion move event in PostHog
      posthog?.capture('player_move', {
        move: userMoveResult.san,
        moveNumber: Math.ceil(updatedGame.history().length / 2),
        playerColor: gameSettings.playerColor,
        botRating: gameSettings.botRating,
        timeControl: timeControl ? `${timeControl.initial / 60000}+${timeControl.increment / 1000}` : 'unlimited',
        fen: updatedGame.fen(),
        isCapture: userMoveResult.san.includes('x'),
        isCheck: updatedGame.isCheck(),
        pieceType: 'pawn',
        isPromotion: true,
        promotionPiece: promotionPiece
      });
    } catch(e: unknown) {
      console.error("Error applying promotion move:", e instanceof Error ? e.message : 'Unknown error');
      setMoveToPromote(null);
      return false;
    }

    try {
      // Get player's clock time before applying the move
      const playerClockTime = updatedGame.history().length % 2 === 1 ? whiteTime : blackTime;
      applyGameInstance(updatedGame, playerClockTime);
      setMoveToPromote(null);

      // Record move data for game storage
      if (userMoveResult) {
        recordMoveData(userMoveResult.san, playerClockTime);
      }

      // Clock management with timeseal for promotion
      // Determine who just moved and who's turn it is now
      const playerWhoJustMoved = updatedGame.history().length % 2 === 1 ? 'white' : 'black';
      const currentTurn = updatedGame.turn() === 'w' ? 'white' : 'black';

      // Use consistent clock management for both first move and subsequent moves
      handleMoveCompletion(playerWhoJustMoved, currentTurn, false);
    } catch (e) {
      console.error("Error updating game state after promotion:", e);
      setMoveToPromote(null);
      return false;
    }

    handleComputerTurn(updatedGame);
    return true;
  }

  // Handles non-promotion moves with AI response
  const onDrop = useCallback((sourceSquare: string, targetSquare: string, piece: string): boolean => {
    if (!isViewingLatest) {
      console.log("Cannot make moves while browsing history.");
      return false;
    }

    if (gameEndedByTime || gameEndedByResignation) {
      console.log("Cannot make moves after game has ended.");
      return false;
    }

    console.log(`onDrop received: source=${sourceSquare}, target=${targetSquare}, piece=${piece}`);

    // Check if player is moving the correct color pieces
    const pieceColor = piece[0]; // 'w' or 'b'
    const expectedColor = gameSettings.playerColor === 'white' ? 'w' : 'b';
    if (pieceColor !== expectedColor) {
      console.log(`Cannot move ${pieceColor === 'w' ? 'white' : 'black'} pieces when playing as ${gameSettings.playerColor}`);
      return false;
    }

    const isPromotion = handlePromotionCheck(sourceSquare, targetSquare, piece);

    // --- Prepare Moves ---
    // 1. Prepare user move data
    const userMoveData = { from: sourceSquare, to: targetSquare };

    // 2. Validate the move
    const validationGame = new Chess(game.fen());
    let userMoveResultTemp: ChessJsMove | null = null;

    try {
      userMoveResultTemp = validationGame.move(userMoveData);
    } catch(e: unknown) {
      console.log("Invalid user move geometry/rules:", e instanceof Error ? e.message : 'Unknown error');
      return false; // Illegal move
    }

    // If valid & promotion, let handlePromotionPieceSelect take over
    if (userMoveResultTemp && isPromotion) {
      console.log("Move is valid and requires promotion. Allowing dialog.");
      return true;
    }

    // Get final validated move in SAN format
    const finalUserMoveSAN = userMoveResultTemp?.san;

    // Create a fresh game with full history preserved
    const updatedGame = new Chess();

    // First load the current game's PGN to preserve full history
    try {
      updatedGame.loadPgn(game.pgn());
    } catch(e: unknown) {
      // Fallback to just loading the current position
      updatedGame.load(game.fen());
      console.warn("Couldn't load PGN, using FEN instead:", e instanceof Error ? e.message : 'Unknown error');
    }

    // Apply user's move
    try {
      updatedGame.move(finalUserMoveSAN!);
      console.log(`User move applied: ${finalUserMoveSAN}, game state now: turn=${updatedGame.turn()}, FEN=${updatedGame.fen()}`);
      
      // Track move event in PostHog
      posthog?.capture('player_move', {
        move: finalUserMoveSAN,
        moveNumber: Math.ceil(updatedGame.history().length / 2),
        playerColor: gameSettings.playerColor,
        botRating: gameSettings.botRating,
        timeControl: timeControl ? `${timeControl.initial / 60000}+${timeControl.increment / 1000}` : 'unlimited',
        fen: updatedGame.fen(),
        isCapture: finalUserMoveSAN?.includes('x'),
        isCheck: updatedGame.isCheck(),
        pieceType: piece[1].toLowerCase()
      });
    } catch(e: unknown) {
      console.error("Error applying user move:", e instanceof Error ? e.message : 'Unknown error');
      return false;
    }

    try {
      // Get player's clock time before applying the move
      const playerClockTime = updatedGame.history().length % 2 === 1 ? whiteTime : blackTime;
      applyGameInstance(updatedGame, playerClockTime);

      // Record move data for game storage
      recordMoveData(finalUserMoveSAN!, playerClockTime);

      // Clock management with timeseal
      // Determine who just moved and who's turn it is now
      const playerWhoJustMoved = updatedGame.history().length % 2 === 1 ? 'white' : 'black';
      const currentTurn = updatedGame.turn() === 'w' ? 'white' : 'black';

      // Use consistent clock management for both first move and subsequent moves
      handleMoveCompletion(playerWhoJustMoved, currentTurn, false);
    } catch (e) {
      console.error("Error updating game state:", e);
      return false;
    }

    handleComputerTurn(updatedGame);
    return true; // Move successful
  }, [isViewingLatest, gameEndedByTime, gameEndedByResignation, game, gameSettings, whiteTime, blackTime, handleMoveCompletion, applyGameInstance, handleComputerTurn, handlePromotionCheck, posthog, timeControl, recordMoveData]);

  // --- Load FEN ---
  const handleLoadFEN = useCallback((fenString: string) => {
    try {
      // Get trimmed FEN string
      const trimmedFen = fenString.trim();

      if (!trimmedFen) {
        console.error("Empty FEN string");
        return;
      }

      // Use the hook's loadGameFromFEN method
      const result = loadGameFromFEN(trimmedFen);
      
      if (!result.valid) {
        setUiState(prev => ({ ...prev, fenError: "Invalid FEN string" }));
        console.error("Invalid FEN string", result.error);
        return;
      }

      const loadedGame = result.game!;
      
      // Set the loaded FEN in the PGN header so it's preserved
      if (typeof (loadedGame as Chess & { header?: (key?: string, value?: string) => Record<string, string> | void }).header === 'function') {
        const gameWithHeader = loadedGame as Chess & { header: (key?: string, value?: string) => Record<string, string> | void };
        gameWithHeader.header('FEN', fenString);
        gameWithHeader.header('SetUp', '1');
        console.log("Set FEN in PGN headers:", gameWithHeader.header());
      } else {
        console.log("Header function not available, FEN will be preserved another way");
        // In tests we won't have header function, so just log this
      }

      // After loading a FEN, make sure to clear the moveHistory properly
      console.log("Loaded FEN, clearing history");

      // Generate empty move history (FEN doesn't contain history)
      // We'll need to manually add any moves made after this point
      // But we'll add customStartFen to tell the MoveList component about our custom position
      const isCustomStartPosition = fenString !== 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';
      const customStartFen = isCustomStartPosition ? fenString : null;
      
      // If it's a custom position, create a special history entry to store the start position
      const newHistory: EnhancedHistoryEntry[] & { customStartFen?: string } = [];
      // First entry will have the custom start FEN for the MoveList component to use
      if (customStartFen) {
        newHistory.customStartFen = customStartFen;
      }

      setMoveHistory(newHistory);
      setViewedPlyIndex(newHistory.length - 1);
      setMoveRecords([]); // Reset move records when loading FEN

      console.log("FEN loaded successfully:", trimmedFen);
      // Close the FEN dialog on successful load
      setIsFENDialogOpen(false);
    } catch (error) {
      const errorMsg = "Invalid FEN string";
      console.error(errorMsg, error);
      // Dialog will show its own error
    }
  }, [loadGameFromFEN, setMoveHistory, setViewedPlyIndex]);

  // --- Handle Resign ---
  const handleResign = useCallback(() => {
    // Determine player's color
    const playerColor = gameSettings.playerColor === 'random'
      ? (game.history().length % 2 === 0 ? 'white' : 'black')  // Approximation
      : gameSettings.playerColor;

    stopClock();
    setGameEndedByResignation(true);
    setResignedPlayer(playerColor);

    // Track analytics
    posthog?.capture('game_finished', {
      result: 'resignation',
      player_resigned: playerColor,
      bot_rating: gameSettings.botRating,
      move_count: game.history().length,
    });
  }, [gameSettings, game, stopClock, posthog]);

  // --- Show Legal Moves Handler ---
  const showLegalMoves = useCallback((piece: string, sourceSquare: string): boolean => {
    // Only calculate legal moves if we're viewing the latest position
    if (!isViewingLatest) return false;
    
    try {
      // Create a temporary game instance with the current position
      const tempGame = new Chess(displayedFen);

      // Check if it's the current player's piece
      // React-chessboard passes piece as "wP", "bN", etc.
      const pieceColor = piece.charAt(0).toLowerCase();
      const currentTurn = tempGame.turn();
      const expectedColor = gameSettings.playerColor === 'white' ? 'w' : 'b';

      // First check if it's the player's color
      if (pieceColor !== expectedColor) {
        console.log(`Cannot move ${pieceColor === 'w' ? 'white' : 'black'} pieces when playing as ${gameSettings.playerColor}`);
        return false;
      }

      // Then check if it's the player's turn
      // currentTurn is 'w' or 'b' for whose turn it is
      if (currentTurn !== expectedColor) {
        console.log("Not your turn");
        console.log(`Legal moves from ${sourceSquare}:`, []);
        return true; // Return true to allow drag for premoves
      }

      // Get all legal moves for the current position
      const moves = tempGame.moves({ verbose: true }) as ChessJsMove[];

      // Filter moves to only include those from the source square
      const legalMovesFromSource = moves.filter(move => move.from === sourceSquare);

      // Extract the destination squares
      const legalDestinations = legalMovesFromSource.map(move => move.to);

      // Create styles for the legal destination squares
      const newSquareStyles: Record<string, React.CSSProperties> = {};
      
      // Apply last move highlighting first (if any)
      if (lastMoveSquares.from) {
        newSquareStyles[lastMoveSquares.from] = {
          backgroundColor: getCSSVariable('--color-square-last-move-pale')
        };
      }
      if (lastMoveSquares.to) {
        newSquareStyles[lastMoveSquares.to] = {
          backgroundColor: getCSSVariable('--color-square-last-move-pale')
        };
      }
      
      // Apply legal move dots
      const legalMoveColor = getCSSVariable('--color-chess-legal') || 'rgba(0, 128, 0, 0.4)';
      
      legalDestinations.forEach(square => {
        // If this is also a last move square, we need to merge styles
        if (square === lastMoveSquares.from || square === lastMoveSquares.to) {
          newSquareStyles[square] = {
            backgroundColor: newSquareStyles[square]?.backgroundColor,
            backgroundImage: `radial-gradient(circle, ${legalMoveColor} 19%, transparent 20%)`,
            backgroundPosition: 'center',
            backgroundRepeat: 'no-repeat',
            backgroundSize: 'contain'
          };
        } else {
          newSquareStyles[square] = {
            backgroundImage: `radial-gradient(circle, ${legalMoveColor} 19%, transparent 20%)`,
            backgroundPosition: 'center',
            backgroundRepeat: 'no-repeat',
            backgroundSize: 'contain'
          };
        }
      });

      // Add a special style for the source square to track which piece we're showing moves for
      // Merge with last move style if it's also a last move square
      if (sourceSquare === lastMoveSquares.from || sourceSquare === lastMoveSquares.to) {
        newSquareStyles[sourceSquare] = {
          ...newSquareStyles[sourceSquare],
          backgroundColor: getCSSVariable('--color-square-selected')
        };
      } else {
        newSquareStyles[sourceSquare] = {
          backgroundColor: getCSSVariable('--color-square-selected')
        };
      }

      // Update state
      setLegalSquares(legalDestinations);
      setSquareStyles(newSquareStyles);
      setSelectedPiece(sourceSquare);

      return true;
    } catch (e) {
      console.error("Error calculating legal moves:", e);
      setLegalSquares([]);
      setSquareStyles({});
      setSelectedPiece(null);
      return false;
    }
  }, [isViewingLatest, displayedFen, gameSettings, lastMoveSquares, setLegalSquares, setSquareStyles, setSelectedPiece]);

  // --- Mouse and Drag Handlers for Legal Move Highlighting ---
  const onMouseDown = useCallback((piece: string, sourceSquare: string): boolean => {
    // Check if we have a piece selected and this is a legal destination
    if (selectedPiece && legalSquares.includes(sourceSquare)) {
      // This is a move completion - clicking on destination that has a piece
      const tempGame = new Chess(displayedFen);
      const sourcePiece = tempGame.get(selectedPiece as Square);
      if (!sourcePiece) return true;
      
      // Make the move
      const pieceNotation = sourcePiece.color === 'w' ? 
        sourcePiece.type.toUpperCase() : sourcePiece.type.toLowerCase();
      const fullPiece = sourcePiece.color + pieceNotation.toUpperCase();
      
      onDrop(selectedPiece, sourceSquare, fullPiece);
      
      // Clear selection
      setLegalSquares([]);
      setSquareStyles({});
      setSelectedPiece(null);
      return true;
    }
    
    // If we're clicking on the same piece that's already selected, hide the legal moves
    if (selectedPiece === sourceSquare && Object.keys(squareStyles).length > 0) {
      setLegalSquares([]);
      setSquareStyles({});
      setSelectedPiece(null);
      return true;
    }
    
    // Show legal moves for this piece
    return showLegalMoves(piece, sourceSquare);
  }, [selectedPiece, squareStyles, showLegalMoves, legalSquares, displayedFen, onDrop, setLegalSquares, setSquareStyles, setSelectedPiece]);

  // Handler for clicking on squares (for click-to-move)
  const onSquareClick = useCallback((square: string) => {
    // If no piece is selected, do nothing
    if (!selectedPiece) return;
    
    // If clicking on the same square as selected piece, deselect
    if (square === selectedPiece) {
      setLegalSquares([]);
      setSquareStyles({});
      setSelectedPiece(null);
      return;
    }
    
    // Check if this square is a legal move destination
    if (legalSquares.includes(square)) {
      // Get the piece at the selected square
      const tempGame = new Chess(displayedFen);
      const piece = tempGame.get(selectedPiece as Square);
      if (!piece) return;
      
      // Make the move
      const pieceNotation = piece.color === 'w' ? 
        piece.type.toUpperCase() : piece.type.toLowerCase();
      const fullPiece = piece.color + pieceNotation.toUpperCase();
      
      onDrop(selectedPiece, square, fullPiece);
    }
    
    // Clear selection after move attempt
    setLegalSquares([]);
    setSquareStyles({});
    setSelectedPiece(null);
  }, [selectedPiece, legalSquares, displayedFen, onDrop, setLegalSquares, setSquareStyles, setSelectedPiece]);

  // Handler for when piece drag ends
  function onDragEnd() {
    // Clear the legal squares when the drag ends
    setLegalSquares([]);
    setSelectedPiece(null);
    
    // Preserve last move highlighting
    const newSquareStyles: Record<string, React.CSSProperties> = {};
    if (lastMoveSquares.from) {
      newSquareStyles[lastMoveSquares.from] = {
        backgroundColor: getCSSVariable('--color-square-last-move-pale')
      };
    }
    if (lastMoveSquares.to) {
      newSquareStyles[lastMoveSquares.to] = {
        backgroundColor: getCSSVariable('--color-square-last-move-pale')
      };
    }
    setSquareStyles(newSquareStyles);
  }

  // --- Navigation Handlers ---
  const handleGoToPly = useCallback((plyIndex: number) => {
    // Clear any piece selection when navigating
    setLegalSquares([]);
    setSelectedPiece(null);
    
    // Update viewed ply index
    setViewedPlyIndex(plyIndex);
    
    // Update last move squares based on the selected move
    if (plyIndex >= 0 && plyIndex < moveHistory.length) {
      const moveData = moveHistory[plyIndex];
      if (moveData && moveData.from && moveData.to) {
        setLastMoveSquares({ from: moveData.from, to: moveData.to });
        
        // Apply highlighting
        const newSquareStyles: Record<string, React.CSSProperties> = {};
        newSquareStyles[moveData.from] = {
          backgroundColor: getCSSVariable('--color-square-navigation')
        };
        newSquareStyles[moveData.to] = {
          backgroundColor: getCSSVariable('--color-square-navigation')
        };
        setSquareStyles(newSquareStyles);
      }
    } else {
      // No move to highlight
      setLastMoveSquares({ from: null, to: null });
      setSquareStyles({});
    }
  }, [moveHistory, setLegalSquares, setSelectedPiece, setViewedPlyIndex, setLastMoveSquares, setSquareStyles]);

  // New game handler
  // State to track if clock was running before edit
  const [wasClockRunning, setWasClockRunning] = useState(false);

  // Time edit handlers
  const handleTimeEdit = useCallback((player: 'white' | 'black') => {
    // Remember if clock was running and pause it while editing
    setWasClockRunning(clockRunning);
    stopClock();
    setTimeEditDialog({ isOpen: true, player });
  }, [clockRunning, stopClock]);

  const handleTimeEditConfirm = useCallback((minutes: number, seconds: number) => {
    const newTime = (minutes * 60 + seconds) * 1000; // Convert to milliseconds
    if (timeEditDialog.player === 'white') {
      setWhiteTime(newTime);
    } else if (timeEditDialog.player === 'black') {
      setBlackTime(newTime);
    }
    setTimeEditDialog({ isOpen: false, player: null });
    
    // Resume clock if it was running before edit
    if (wasClockRunning && activeColor) {
      setClockRunning(true);
    }
  }, [timeEditDialog.player, setWhiteTime, setBlackTime, wasClockRunning, activeColor, setClockRunning]);

  const handleNewGame = useCallback((settings: {
    rating: number;
    color: 'white' | 'black' | 'random';
    timeControl?: TimeControl;
  }) => {
    // Resolve random color to actual color
    const actualColor = settings.color === 'random' 
      ? (Math.random() < 0.5 ? 'white' : 'black')
      : settings.color;
    
    // Update settings with resolved color
    setGameSettings(prev => ({ ...prev, botRating: settings.rating, playerColor: actualColor }));
    if (settings.timeControl) {
      setTimeControl(settings.timeControl);
      // Reset clock with new time control
      resetClock(settings.timeControl.initial);
    }
    
    // Reset the game with new settings
    const newGame = resetGame();
    setIsNewGameDialogOpen(false);
    setHasPlayedTimeWarning(false); // Reset time warning for new game
    setHasRecordedGameResult(false); // Reset rating recording flag for new game
    setGameEndedByResignation(false); // Reset resignation state
    setResignedPlayer(null);
    setMoveRecords([]); // Reset move records for new game
    
    // Track new game event in PostHog
    posthog?.capture('new_game_started', {
      playerColor: actualColor,
      selectedColor: settings.color, // Track if they chose random
      botRating: settings.rating,
      timeControl: settings.timeControl ? `${settings.timeControl.initial / 60000}+${settings.timeControl.increment / 1000}` : 'unlimited',
      timeControlMinutes: settings.timeControl ? settings.timeControl.initial / 60000 : null,
      timeControlIncrement: settings.timeControl ? settings.timeControl.increment / 1000 : null,
      wasRandomColor: settings.color === 'random'
    });
    
    // If player selected black, bot should make first move
    if (actualColor === 'black') {
      // Need a small delay to ensure the board has updated with the new color
      setTimeout(() => {
        // Start the clock for white (bot's turn)
        setActiveColor('white');
        setClockRunning(true);
        handleComputerTurn(newGame);
      }, 100);
    }
  }, [resetGame, resetClock, handleComputerTurn, setActiveColor, setClockRunning, posthog]);


  // Force FEN update when game or history changes
  useEffect(() => {
    if (game && isViewingLatest) {
      // Make sure the board is showing latest position
      game.fen(); // This triggers a re-render if needed
    }
  }, [game, isViewingLatest]);

  // Keyboard Navigation Effect
  useEffect(() => {
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'ArrowLeft') {
        // Move one ply back
        const newPlyIndex = Math.max(-1, viewedPlyIndex - 1);
        handleGoToPly(newPlyIndex);
      } else if (event.key === 'ArrowRight') {
        // Move one ply forward
        const newPlyIndex = Math.min(moveHistory.length - 1, viewedPlyIndex + 1);
        handleGoToPly(newPlyIndex);
      }
    };
    
    window.addEventListener('keydown', handleKeyDown);
    return () => { window.removeEventListener('keydown', handleKeyDown); };
  }, [moveHistory.length, viewedPlyIndex, handleGoToPly]);

  // Stop clocks when viewing history
  useEffect(() => {
    if (!isViewingLatest) {
      stopClock();
    }
  }, [isViewingLatest, stopClock]);

  // Stop clocks when game ends - separate effects to avoid dependency array issues
  useEffect(() => {
    if (game.isGameOver()) {
      stopClock();
      
      // Track game finished event in PostHog
      posthog?.capture('game_finished', {
        result: game.isCheckmate() ? (game.turn() === 'w' ? 'black_wins' : 'white_wins') : 
               game.isStalemate() ? 'stalemate' :
               game.isInsufficientMaterial() ? 'insufficient_material' :
               game.isThreefoldRepetition() ? 'threefold_repetition' :
               game.isDraw() ? 'draw' : 'unknown',
        moveCount: game.history().length,
        playerColor: gameSettings.playerColor,
        botRating: gameSettings.botRating,
        timeControl: timeControl ? `${timeControl.initial / 60000}+${timeControl.increment / 1000}` : 'unlimited',
        finalFEN: game.fen(),
        isCheckmate: game.isCheckmate(),
        isStalemate: game.isStalemate(),
        isDraw: game.isDraw()
      });
    }
  }, [game, stopClock, posthog, gameSettings.playerColor, gameSettings.botRating, timeControl]);

  // Stop clock when time runs out
  useEffect(() => {
    if (gameEndedByTime) {
      stopClock();

      // Track game finished by time event in PostHog
      posthog?.capture('game_finished', {
        result: timeWinner === 'white' ? 'white_wins' : 'black_wins',
        reason: 'time_forfeit',
        moveCount: game.history().length,
        playerColor: gameSettings.playerColor,
        botRating: gameSettings.botRating,
        timeControl: timeControl ? `${timeControl.initial / 60000}+${timeControl.increment / 1000}` : 'unlimited',
        finalFEN: game.fen(),
        loserTimeRemaining: 0,
        winnerTimeRemaining: timeWinner === 'white' ? whiteTime : blackTime
      });
    }
  }, [gameEndedByTime, stopClock, posthog, timeWinner, game, gameSettings.playerColor, gameSettings.botRating, timeControl, whiteTime, blackTime]);

  // Record game result for rating when game ends
  useEffect(() => {
    // Only run if game has ended and we haven't recorded yet
    const gameIsOver = game.isGameOver() || gameEndedByTime || gameEndedByResignation;
    if (!gameIsOver || hasRecordedGameResult) return;

    // Determine the result from the player's perspective
    let result: 'win' | 'loss' | 'draw';
    let resultReason: string | undefined;
    const playerColor = gameSettings.playerColor;

    if (gameEndedByResignation) {
      // Resignation - always a loss for the player
      result = 'loss';
      resultReason = 'Resignation';
    } else if (gameEndedByTime) {
      // Time forfeit
      const playerWon = timeWinner === playerColor;
      result = playerWon ? 'win' : 'loss';
      resultReason = playerWon ? 'Opponent flagged' : 'Time forfeit';
    } else if (game.isCheckmate()) {
      // Checkmate - winner is the player who just moved (opposite of whose turn it is)
      const winnerColor = game.turn() === 'w' ? 'black' : 'white';
      result = winnerColor === playerColor ? 'win' : 'loss';
      resultReason = 'Checkmate';
    } else if (game.isStalemate()) {
      result = 'draw';
      resultReason = 'Stalemate';
    } else if (game.isInsufficientMaterial()) {
      result = 'draw';
      resultReason = 'Insufficient material';
    } else if (game.isThreefoldRepetition()) {
      result = 'draw';
      resultReason = 'Threefold repetition';
    } else if (game.isDraw()) {
      result = 'draw';
      resultReason = 'Draw';
    } else {
      // Unknown end condition
      result = 'draw';
    }

    // Mark that we've recorded this game
    setHasRecordedGameResult(true);

    // Record the game result
    recordGameResult({
      botRating: gameSettings.botRating,
      playerColor: playerColor === 'random' ? 'white' : playerColor,
      result,
      resultReason,
      moveCount: game.history().length,
      timeControl: timeControl ? `${timeControl.initial / 60000}+${timeControl.increment / 1000}` : undefined,
      gameMoves: moveRecords,
    }).then((update) => {
      // Show the rating modal if we got an update
      if (update) {
        setRatingModalState({
          isOpen: true,
          gameResult: result,
          resultReason,
        });
      }
    });
  }, [game, gameEndedByTime, gameEndedByResignation, timeWinner, gameSettings.playerColor, gameSettings.botRating, timeControl, hasRecordedGameResult, recordGameResult, moveRecords]);

  // Fetch evaluation when position changes
  useEffect(() => {
    try {
      const fen = displayedFen;
      const moves = isViewingLatest ? game.history() : moveHistory.slice(0, viewedPlyIndex + 1).map(m => m.san);
      fetchEvaluation(fen, moves);
    } catch (error: unknown) {
      console.error('Error in evaluation effect:', error);
      setEvalState(prev => ({ ...prev, error: error instanceof Error ? error.message : 'Unknown error', loading: false }));
    }
  }, [displayedFen, isViewingLatest]); // eslint-disable-line react-hooks/exhaustive-deps

  // Update debug state whenever relevant state changes
  useEffect(() => {
    updateDebugState();
  }, [updateDebugState]);

  // Expose debug function
  useEffect(() => {
    if (typeof window !== 'undefined') {
      window.__exportPGN = () => {
        const pgn = generatePGNWithClocks({
          game,
          moveHistory,
          moveTimes,
          playerColor: gameSettings.playerColor === 'random' ? 'white' : gameSettings.playerColor,
          botRating: gameSettings.botRating,
          timeControl,
          gameEndedByTime,
          timeWinner
        });
        console.log('PGN:', pgn);
        return pgn;
      };
    }
  }, [moveHistory, game, gameSettings, moveTimes, timeControl, gameEndedByTime, timeWinner]);

  const [hasPlayedTimeWarning, setHasPlayedTimeWarning] = useState(false);

  useEffect(() => {
    // Only play warning for the user (not the bot) when crossing 15 seconds
    const userColor = gameSettings.playerColor;
    const userTime = userColor === 'white' ? whiteTime : blackTime;
    
    // Check if user just crossed below 15 seconds and hasn't been warned yet
    if (userTime <= 15000 && userTime > 0 && !hasPlayedTimeWarning && clockRunning) {
      // Only play sound for the user, not the bot
      handleTimeWarning();
      setHasPlayedTimeWarning(true);
    }
  }, [whiteTime, blackTime, gameSettings.playerColor, hasPlayedTimeWarning, clockRunning, handleTimeWarning]);

  // Check if mobile
  const [isMobile, setIsMobile] = useState(false);
  const [showMobileMenu, setShowMobileMenu] = useState(false);

  useEffect(() => {
    const checkMobile = () => {
      setIsMobile(window.innerWidth <= 767);
    };
    checkMobile();
    window.addEventListener('resize', checkMobile);
    return () => window.removeEventListener('resize', checkMobile);
  }, []);

  return (
    <div className={isMobile ? "d-flex flex-column h-100 w-100 position-relative" : "d-flex h-100 w-100 p-3 overflow-hidden"} style={{ minHeight: 0, maxHeight: '100%' }}>
      {/* Mobile Header with Hamburger Menu */}
      {isMobile && (
        <div className="bg-dark border-bottom border-secondary p-2 d-flex justify-content-between align-items-center">
          <GameStatus
            game={game}
            gameEndedByTime={gameEndedByTime}
            timeWinner={timeWinner}
            gameEndedByResignation={gameEndedByResignation}
            resignedPlayer={resignedPlayer}
          />
          <button
            className="btn btn-sm btn-outline-light"
            onClick={() => setShowMobileMenu(!showMobileMenu)}
          >
            <i className={`bi bi-${showMobileMenu ? 'x' : 'list'}`}></i>
          </button>
        </div>
      )}

      {/* Mobile Menu Overlay */}
      {isMobile && showMobileMenu && (
        <div
          className="position-fixed top-0 start-0 w-100 h-100 bg-dark bg-opacity-75"
          style={{ zIndex: 1040 }}
          onClick={() => setShowMobileMenu(false)}
        >
          <div
            className="position-absolute top-0 end-0 bg-dark border-start border-secondary h-100"
            style={{ width: '250px', maxWidth: '80vw' }}
            onClick={(e) => e.stopPropagation()}
          >
            <div className="p-3 d-flex flex-column h-100">
              <div className="d-flex justify-content-between align-items-center mb-3">
                <h5 className="text-light mb-0">Menu</h5>
                <button
                  className="btn btn-sm btn-outline-light"
                  onClick={() => setShowMobileMenu(false)}
                >
                  <i className="bi bi-x"></i>
                </button>
              </div>
              
              {/* Site branding */}
              <div className="mb-4 pb-3 border-bottom border-secondary">
                <Link href="/" className="text-decoration-none text-light fs-4 fw-bold">ChessMimic</Link>
              </div>
              
              {/* Game Controls */}
              <div className="mb-4">
                <h6 className="text-secondary mb-3">Game Options</h6>
                <GameControls
                  onOpenFENDialog={() => {
                    setIsFENDialogOpen(true);
                    setShowMobileMenu(false);
                  }}
                  onNewGame={() => {
                    stopClock();
                    setIsNewGameDialogOpen(true);
                    setShowMobileMenu(false);
                  }}
                  onExportPGN={() => {
                    exportPGN();
                    setShowMobileMenu(false);
                  }}
                  onResign={() => {
                    handleResign();
                    setShowMobileMenu(false);
                  }}
                  gameInProgress={!game.isGameOver() && !gameEndedByTime && !gameEndedByResignation}
                  soundEnabled={gameSettings.soundEnabled}
                  onSoundEnabledChange={(enabled) => setGameSettings(prev => ({ ...prev, soundEnabled: enabled }))}
                />
              </div>
              
              {/* Auth section - spacer to push to bottom */}
              <div className="mt-auto pt-3 border-top border-secondary">
                {process.env.NEXT_PUBLIC_DISABLE_AUTH === 'true' ? (
                  <>
                    <div className="d-grid gap-2">
                      <button className="btn btn-primary">Sign In</button>
                      <button className="btn btn-outline-secondary">Sign Up</button>
                    </div>
                  </>
                ) : (
                  <>
                    <SignedOut>
                      <div className="d-grid gap-2">
                        <SignInButton mode="modal">
                          <button className="btn btn-primary">Sign In</button>
                        </SignInButton>
                        <SignUpButton mode="modal">
                          <button className="btn btn-outline-secondary">Sign Up</button>
                        </SignUpButton>
                      </div>
                    </SignedOut>
                    <SignedIn>
                      <div className="d-flex justify-content-center">
                        <CustomUserButton />
                      </div>
                    </SignedIn>
                  </>
                )}
                <div className="text-center mt-3">
                  <small className="text-muted">Play chess against AI that mimics human play</small>
                </div>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Main Layout Container */}
      <div className={isMobile ? "flex-grow-1 d-flex flex-column overflow-hidden" : "d-flex gap-3 w-100 h-100 overflow-hidden"}>
        {isMobile ? (
          <>
            {/* Mobile Layout */}
            <div className="flex-grow-1 d-flex flex-column p-1" style={{ minHeight: 0 }}>
              {/* Opponent Info */}
              <PlayerInfoPanel
                name="Opponent"
                rating={gameSettings.botRating}
                time={gameSettings.playerColor === 'white' ? blackTime : whiteTime}
                isActive={activeColor === (gameSettings.playerColor === 'white' ? 'black' : 'white')}
                isBot
                onEditTime={() => handleTimeEdit(gameSettings.playerColor === 'white' ? 'black' : 'white')}
                variant="mobile"
                timeTestId={gameSettings.playerColor === 'white' ? 'black-time' : 'white-time'}
              />
              
              {/* Chess Board - Takes most space */}
              <div className="flex-grow-1 d-flex align-items-center justify-content-center py-1" style={{ minHeight: 0 }}>
                <ChessboardArea
                  className="w-100 h-100"
                  displayedFen={displayedFen}
                  evaluation={evalState.evaluation}
                  evalLoading={evalState.loading}
                  evalError={evalState.error}
                  evalBlackProb={evalState.blackProb}
                  evalDrawProb={evalState.drawProb}
                  evalWhiteProb={evalState.whiteProb}
                  isWhiteToMove={game.turn() === 'w'}
                  onPieceDrop={onDrop}
                  onPromotionCheck={handlePromotionCheck}
                  onPromotionPieceSelect={handlePromotionPieceSelect}
                  showLegalMoves={showLegalMoves}
                  onDragEnd={onDragEnd}
                  onMouseDown={onMouseDown}
                  onSquareClick={onSquareClick}
                  squareStyles={squareStyles}
                  lastMoveSquares={lastMoveSquares}
                  playerColor={gameSettings.playerColor === 'random' ? 'white' : gameSettings.playerColor}
                  isViewingLatest={isViewingLatest}
                  onBoardHeightChange={setBoardHeight}
                />
              </div>

              {/* Current Player Info */}
              <PlayerInfoPanel
                name="You"
                rating={userRating?.rating ?? 1500}
                time={gameSettings.playerColor === 'white' ? whiteTime : blackTime}
                isActive={activeColor === gameSettings.playerColor}
                onEditTime={() => handleTimeEdit(gameSettings.playerColor === 'white' ? 'white' : 'black')}
                variant="mobile"
                timeTestId={gameSettings.playerColor === 'white' ? 'white-time' : 'black-time'}
              />
            </div>
            
            {/* Move List at Bottom */}
            <div className="bg-dark border-top border-secondary" style={{ height: '60px' }}>
              <MoveHistory
                moveHistory={enhancedHistory}
                viewedPlyIndex={viewedPlyIndex}
                onGoToPly={navigateToPly}
                boardHeight={null}
                isMobileLayout={true}
              />
            </div>
          </>
        ) : (
          <>
            {/* Desktop Layout */}
            {/* Left Section: Chessboard with Player Info */}
            <div className="d-flex flex-column gap-3 overflow-hidden" style={{ flex: '1 1 60%', minWidth: 0, minHeight: 0 }}>
              <PlayerInfoPanel
                name="Opponent"
                rating={gameSettings.botRating}
                time={gameSettings.playerColor === 'white' ? blackTime : whiteTime}
                isActive={activeColor === (gameSettings.playerColor === 'white' ? 'black' : 'white')}
                isBot
                onEditTime={() => handleTimeEdit(gameSettings.playerColor === 'white' ? 'black' : 'white')}
                timeTestId={gameSettings.playerColor === 'white' ? 'black-time' : 'white-time'}
              />
              <ChessboardArea
                className="flex-grow-1 overflow-hidden d-flex"
                displayedFen={displayedFen}
                evaluation={evalState.evaluation}
                evalLoading={evalState.loading}
                evalError={evalState.error}
                evalBlackProb={evalState.blackProb}
                evalDrawProb={evalState.drawProb}
                evalWhiteProb={evalState.whiteProb}
                isWhiteToMove={game.turn() === 'w'}
                onPieceDrop={onDrop}
                onPromotionCheck={handlePromotionCheck}
                onPromotionPieceSelect={handlePromotionPieceSelect}
                showLegalMoves={showLegalMoves}
                onDragEnd={onDragEnd}
                onMouseDown={onMouseDown}
                onSquareClick={onSquareClick}
                squareStyles={squareStyles}
                lastMoveSquares={lastMoveSquares}
                playerColor={gameSettings.playerColor === 'random' ? 'white' : gameSettings.playerColor}
                isViewingLatest={isViewingLatest}
                onBoardHeightChange={setBoardHeight}
              />
              <PlayerInfoPanel
                name="You"
                rating={userRating?.rating ?? 1500}
                time={gameSettings.playerColor === 'white' ? whiteTime : blackTime}
                isActive={activeColor === gameSettings.playerColor}
                onEditTime={() => handleTimeEdit(gameSettings.playerColor === 'white' ? 'white' : 'black')}
                timeTestId={gameSettings.playerColor === 'white' ? 'white-time' : 'black-time'}
              />
            </div>

            {/* Right Section: MoveHistory and GameControls */}
            <div className="d-flex flex-column gap-3 bg-dark border border-secondary rounded p-3 h-100 overflow-hidden" style={{ flex: '0 1 350px', minWidth: '280px' }}>
              <GameStatus
                game={game}
                gameEndedByTime={gameEndedByTime}
                timeWinner={timeWinner}
                gameEndedByResignation={gameEndedByResignation}
                resignedPlayer={resignedPlayer}
              />
              <div className="flex-fill overflow-hidden d-flex flex-column gap-3">
                <MoveHistory
                  moveHistory={enhancedHistory}
                  viewedPlyIndex={viewedPlyIndex}
                  onGoToPly={navigateToPly}
                  boardHeight={null}
                />
              </div>
              <div className="flex-shrink-0">
                <GameControls
                  onOpenFENDialog={() => setIsFENDialogOpen(true)}
                  onNewGame={() => {
                    stopClock();
                    setIsNewGameDialogOpen(true);
                  }}
                  onExportPGN={exportPGN}
                  onResign={handleResign}
                  gameInProgress={!game.isGameOver() && !gameEndedByTime && !gameEndedByResignation}
                  soundEnabled={gameSettings.soundEnabled}
                  onSoundEnabledChange={(enabled) => setGameSettings(prev => ({ ...prev, soundEnabled: enabled }))}
                />
              </div>
            </div>
          </>
        )}
      </div>

      {/* New Game Dialog */}
      <NewGameDialog
        isOpen={isNewGameDialogOpen}
        onClose={() => setIsNewGameDialogOpen(false)}
        onConfirm={handleNewGame}
        currentRating={gameSettings.botRating}
        currentColor={gameSettings.playerColor}
        currentTimeControl={timeControl}
      />

      {/* FEN Dialog */}
      <FENDialog
        isOpen={isFENDialogOpen}
        onClose={() => setIsFENDialogOpen(false)}
        onLoadFEN={handleLoadFEN}
        currentFEN={displayedFen}
      />

      {/* Time Edit Dialog */}
      <TimeEditDialog
        isOpen={timeEditDialog.isOpen}
        onClose={() => {
          setTimeEditDialog({ isOpen: false, player: null });
          // Resume clock if it was running before edit
          if (wasClockRunning && activeColor) {
            setClockRunning(true);
          }
        }}
        onConfirm={handleTimeEditConfirm}
        currentTime={timeEditDialog.player === 'white' ? whiteTime : blackTime}
        playerName={timeEditDialog.player === 'white' ? 'White' : 'Black'}
      />

      {/* Rating Change Modal */}
      <RatingChangeModal
        isOpen={ratingModalState.isOpen}
        onClose={() => {
          setRatingModalState(prev => ({ ...prev, isOpen: false }));
          clearLastRatingUpdate();
        }}
        onPlayAgain={() => {
          setRatingModalState(prev => ({ ...prev, isOpen: false }));
          clearLastRatingUpdate();
          setIsNewGameDialogOpen(true);
        }}
        ratingUpdate={lastRatingUpdate}
        gameResult={ratingModalState.gameResult}
        botRating={gameSettings.botRating}
        resultReason={ratingModalState.resultReason}
      />
    </div>
  );
}
