import React from 'react';
import { render, screen } from '@testing-library/react';
import '@testing-library/jest-dom';
import GameStatus from './GameStatus';
import { Chess } from 'chess.js';

describe('GameStatus', () => {
  it('should show Check! when in check but not checkmate', () => {
    const game = new Chess('3qk3/8/8/8/8/8/8/3K4 w - - 0 1'); // White king in check
    render(<GameStatus game={game} gameEndedByTime={false} timeWinner={null} gameEndedByResignation={false} resignedPlayer={null} />);

    expect(screen.getByText('Check!')).toBeInTheDocument();
    expect(screen.queryByText('Checkmate!')).not.toBeInTheDocument();
    expect(screen.queryByText('Game Over!')).not.toBeInTheDocument();
  });

  it('should show Game Over! and Checkmate! when white wins', () => {
    const game = new Chess('R5k1/5ppp/8/8/8/8/8/7K b - - 0 1'); // Black is checkmated
    render(<GameStatus game={game} gameEndedByTime={false} timeWinner={null} gameEndedByResignation={false} resignedPlayer={null} />);

    expect(screen.getByText('Game Over!')).toBeInTheDocument();
    expect(screen.getByText('Checkmate! White wins!')).toBeInTheDocument();
  });

  it('should show Game Over! and Checkmate! when black wins', () => {
    const game = new Chess('rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3'); // White is checkmated
    render(<GameStatus game={game} gameEndedByTime={false} timeWinner={null} gameEndedByResignation={false} resignedPlayer={null} />);

    expect(screen.getByText('Game Over!')).toBeInTheDocument();
    expect(screen.getByText('Checkmate! Black wins!')).toBeInTheDocument();
  });

  it('should show Game Over! and Draw! for stalemate', () => {
    const game = new Chess('4k3/8/8/8/8/8/8/4K3 w - - 0 1'); // King vs King - draw
    render(<GameStatus game={game} gameEndedByTime={false} timeWinner={null} gameEndedByResignation={false} resignedPlayer={null} />);

    expect(screen.getByText('Game Over!')).toBeInTheDocument();
    expect(screen.getByText('Draw!')).toBeInTheDocument();
    expect(screen.queryByText('Check!')).not.toBeInTheDocument();
  });

  it('should show time win when game ended by time', () => {
    const game = new Chess();
    render(<GameStatus game={game} gameEndedByTime={true} timeWinner="white" gameEndedByResignation={false} resignedPlayer={null} />);

    expect(screen.getByText('Game Over!')).toBeInTheDocument();
    expect(screen.getByText('White wins on time!')).toBeInTheDocument();
  });

  it('should show nothing when game is in progress', () => {
    const game = new Chess();
    const { container } = render(<GameStatus game={game} gameEndedByTime={false} timeWinner={null} gameEndedByResignation={false} resignedPlayer={null} />);

    expect(container.textContent).toBe('');
  });
});