import React from 'react';
import { render, screen, fireEvent } from '@testing-library/react';
import '@testing-library/jest-dom';
import NewGameDialog from './NewGameDialog';

describe('NewGameDialog', () => {
  const mockOnClose = jest.fn();
  const mockOnConfirm = jest.fn();

  beforeEach(() => {
    jest.clearAllMocks();
  });

  it('should display the currentRating prop value', () => {
    render(
      <NewGameDialog
        isOpen={true}
        onClose={mockOnClose}
        onConfirm={mockOnConfirm}
        currentRating={1500}
      />
    );

    const ratingInput = screen.getByRole('spinbutton');
    expect(ratingInput).toHaveValue(1500);
  });

  it('should update displayed rating when currentRating prop changes', () => {
    const { rerender } = render(
      <NewGameDialog
        isOpen={true}
        onClose={mockOnClose}
        onConfirm={mockOnConfirm}
        currentRating={1800}
      />
    );

    // Initially should show 1800
    const ratingInput = screen.getByRole('spinbutton');
    expect(ratingInput).toHaveValue(1800);

    // Rerender with new currentRating (simulating user rating loading)
    rerender(
      <NewGameDialog
        isOpen={true}
        onClose={mockOnClose}
        onConfirm={mockOnConfirm}
        currentRating={1500}
      />
    );

    // Should now show 1500
    expect(ratingInput).toHaveValue(1500);
  });

  it('should preserve user selection when currentRating changes after user interaction', () => {
    const { rerender } = render(
      <NewGameDialog
        isOpen={true}
        onClose={mockOnClose}
        onConfirm={mockOnConfirm}
        currentRating={1800}
      />
    );

    const ratingInput = screen.getByRole('spinbutton');

    // User manually changes rating to 2000
    fireEvent.change(ratingInput, { target: { value: '2000' } });
    expect(ratingInput).toHaveValue(2000);

    // currentRating prop changes, but user selection should be preserved
    rerender(
      <NewGameDialog
        isOpen={true}
        onClose={mockOnClose}
        onConfirm={mockOnConfirm}
        currentRating={1500}
      />
    );

    // User's selection of 2000 should be preserved
    expect(ratingInput).toHaveValue(2000);
  });

  it('should call onConfirm with selected rating', () => {
    render(
      <NewGameDialog
        isOpen={true}
        onClose={mockOnClose}
        onConfirm={mockOnConfirm}
        currentRating={1500}
      />
    );

    const startButton = screen.getByText('Start Game');
    fireEvent.click(startButton);

    expect(mockOnConfirm).toHaveBeenCalledWith(
      expect.objectContaining({
        rating: 1500,
      })
    );
  });
});
