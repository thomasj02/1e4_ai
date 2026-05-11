'use client';

import React, { useState, useEffect } from 'react';
import { Modal, Button, Form } from 'react-bootstrap';
import { usePostHog } from 'posthog-js/react';

interface FENDialogProps {
  isOpen: boolean;
  onClose: () => void;
  currentFEN: string;
  onLoadFEN: (fen: string) => void;
}

const FENDialog: React.FC<FENDialogProps> = ({
  isOpen,
  onClose,
  currentFEN,
  onLoadFEN
}) => {
  const [fenInput, setFenInput] = useState('');
  const [error, setError] = useState<string | null>(null);
  const posthog = usePostHog();

  useEffect(() => {
    if (isOpen) {
      setFenInput('');
      setError(null);
    }
  }, [isOpen]);

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    const trimmedFen = fenInput.trim();
    
    if (!trimmedFen) {
      setError('Please enter a FEN string');
      return;
    }

    // Basic FEN validation - check if it has the right number of parts
    const fenParts = trimmedFen.split(' ');
    if (fenParts.length < 1 || fenParts.length > 6) {
      setError('Invalid FEN format');
      return;
    }

    onLoadFEN(trimmedFen);
    onClose();
  };

  const handleCopyFEN = () => {
    navigator.clipboard.writeText(currentFEN).then(() => {
      // Could add a toast notification here
      console.log('FEN copied to clipboard');
      
      // Track FEN export event in PostHog
      posthog?.capture('fen_exported', {
        fen: currentFEN,
        method: 'clipboard_copy'
      });
    }).catch(err => {
      console.error('Failed to copy FEN:', err);
    });
  };

  if (!isOpen) return null;

  return (
    <Modal show={isOpen} onHide={onClose} size="lg" centered>
      <Modal.Header closeButton>
        <Modal.Title>FEN Position</Modal.Title>
      </Modal.Header>
      <Modal.Body>
        <div className="mb-4">
          <Form.Label className="fw-semibold">Current Position</Form.Label>
          <div className="d-flex gap-2">
            <Form.Control
              type="text"
              value={currentFEN}
              readOnly
              className="font-monospace"
              onClick={(e) => (e.target as HTMLInputElement).select()}
            />
            <Button
              variant="outline-secondary"
              onClick={handleCopyFEN}
              title="Copy to clipboard"
            >
              <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor">
                <path d="M4 1.5H3a2 2 0 0 0-2 2V14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V3.5a2 2 0 0 0-2-2h-1v1h1a1 1 0 0 1 1 1V14a1 1 0 0 1-1 1H3a1 1 0 0 1-1-1V3.5a1 1 0 0 1 1-1h1v-1z"/>
                <path d="M9.5 1a.5.5 0 0 1 .5.5v1a.5.5 0 0 1-.5.5h-3a.5.5 0 0 1-.5-.5v-1a.5.5 0 0 1 .5-.5h3zm-3-1A1.5 1.5 0 0 0 5 1.5v1A1.5 1.5 0 0 0 6.5 4h3A1.5 1.5 0 0 0 11 2.5v-1A1.5 1.5 0 0 0 9.5 0h-3z"/>
              </svg>
            </Button>
          </div>
        </div>

        <Form onSubmit={handleSubmit}>
          <Form.Group>
            <Form.Label className="fw-semibold">Load New Position</Form.Label>
            <Form.Control
              type="text"
              value={fenInput}
              onChange={(e) => {
                setFenInput(e.target.value);
                setError(null);
              }}
              placeholder="Paste FEN string here"
              className="font-monospace"
              isInvalid={!!error}
            />
            {error && (
              <Form.Control.Feedback type="invalid">
                {error}
              </Form.Control.Feedback>
            )}
          </Form.Group>
        </Form>
      </Modal.Body>
      <Modal.Footer>
        <Button variant="secondary" onClick={onClose}>
          Cancel
        </Button>
        <Button variant="primary" onClick={handleSubmit}>
          Load Position
        </Button>
      </Modal.Footer>
    </Modal>
  );
};

export default FENDialog;