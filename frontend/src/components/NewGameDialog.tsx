'use client';

import React, { useState, useEffect, useRef } from 'react';
import { Modal, Button, Form, ButtonGroup, Row, Col } from 'react-bootstrap';
import type { NewGameDialogProps, TimeControl } from '../types';

interface RatingPreset {
  value: number;
  label: string;
}

interface TimeControlPreset extends TimeControl {
  label: string;
  name: string;
}

const NewGameDialog: React.FC<NewGameDialogProps> = ({ 
  isOpen, 
  onClose, 
  onConfirm, 
  currentRating = 1800,
  currentColor = 'white',
  currentTimeControl = { initial: 300000, increment: 3000 }
}) => {
  const [selectedRating, setSelectedRating] = useState(currentRating);
  const [selectedColor, setSelectedColor] = useState(currentColor);
  const [selectedTimeControl, setSelectedTimeControl] = useState(currentTimeControl);
  const userHasChangedRating = useRef(false);

  // Sync selectedRating with currentRating prop when it changes (unless user modified it)
  useEffect(() => {
    if (!userHasChangedRating.current) {
      setSelectedRating(currentRating);
    }
  }, [currentRating]);

  const handleRatingChange = (newRating: number) => {
    userHasChangedRating.current = true;
    setSelectedRating(newRating);
  };

  // Common rating presets
  const ratingPresets: RatingPreset[] = [
    { value: 800, label: 'Beginner' },
    { value: 1200, label: 'Intermediate' },
    { value: 1600, label: 'Advanced' },
    { value: 2000, label: 'Expert' },
    { value: 2400, label: 'Master' }
  ];
  
  // Common time control presets
  const timeControlPresets: TimeControlPreset[] = [
    { initial: 60000, increment: 0, label: '1+0', name: 'Bullet' },
    { initial: 180000, increment: 0, label: '3+0', name: 'Blitz' },
    { initial: 180000, increment: 2000, label: '3+2', name: 'Blitz' },
    { initial: 300000, increment: 0, label: '5+0', name: 'Blitz' },
    { initial: 300000, increment: 3000, label: '5+3', name: 'Blitz' },
    { initial: 600000, increment: 0, label: '10+0', name: 'Rapid' },
    { initial: 900000, increment: 10000, label: '15+10', name: 'Rapid' },
    { initial: 1800000, increment: 0, label: '30+0', name: 'Classical' }
  ];
  
  const handleConfirm = () => {
    onConfirm({
      rating: selectedRating,
      color: selectedColor,
      timeControl: selectedTimeControl
    });
    onClose();
  };
  
  const handleCancel = () => {
    // Reset to current values
    setSelectedRating(currentRating);
    setSelectedColor(currentColor);
    setSelectedTimeControl(currentTimeControl);
    userHasChangedRating.current = false;
    onClose();
  };
  
  return (
    <Modal show={isOpen} onHide={handleCancel} size="lg" centered>
      <Modal.Header closeButton>
        <Modal.Title className="w-100 text-center">New Game</Modal.Title>
      </Modal.Header>
      <Modal.Body>
        
        {/* Bot Rating Section */}
        <div className="mb-4">
          <Form.Label className="text-uppercase small fw-semibold">
            Bot Rating
          </Form.Label>
          
          <div className="d-flex align-items-center gap-3 mb-3">
            <Form.Range
              min="400"
              max="3500"
              step="50"
              value={selectedRating}
              onChange={(e) => handleRatingChange(Number(e.target.value))}
              className="flex-fill"
            />
            <Form.Control
              type="number"
              min="400"
              max="3500"
              step="50"
              value={selectedRating}
              onChange={(e) => handleRatingChange(Number(e.target.value))}
              size="sm"
              className="text-center fw-bold"
              style={{ width: '80px' }}
            />
          </div>
          
          <ButtonGroup className="d-flex flex-wrap gap-2">
            {ratingPresets.map(preset => (
              <Button
                key={preset.value}
                size="sm"
                variant={selectedRating === preset.value ? 'primary' : 'outline-secondary'}
                onClick={() => handleRatingChange(preset.value)}
                className="d-flex flex-column py-2"
              >
                {preset.label}
                <span className="small opacity-75">{preset.value}</span>
              </Button>
            ))}
          </ButtonGroup>
        </div>
        
        {/* Time Control Section */}
        <div className="mb-4">
          <Form.Label className="text-uppercase small fw-semibold">
            Time Control
          </Form.Label>
          
          <Row xs={4} className="g-2">
            {timeControlPresets.map((preset, index) => (
              <Col key={index}>
                <Button
                  size="sm"
                  variant={selectedTimeControl.initial === preset.initial && selectedTimeControl.increment === preset.increment ? 'primary' : 'outline-secondary'}
                  onClick={() => setSelectedTimeControl({ initial: preset.initial, increment: preset.increment })}
                  className="w-100 d-flex flex-column py-2"
                >
                  <span className="fw-bold">{preset.label}</span>
                  <span className="small opacity-75">{preset.name}</span>
                </Button>
              </Col>
            ))}
          </Row>
        </div>
        
        {/* Color Selection Section */}
        <div className="mb-4">
          <Form.Label className="text-uppercase small fw-semibold">
            Play as
          </Form.Label>
          
          <div className="d-flex gap-2 justify-content-center">
            <Button
              variant={selectedColor === 'white' ? 'primary' : 'outline-secondary'}
              onClick={() => setSelectedColor('white')}
              aria-label="Play as White"
              className="flex-fill p-2 d-flex flex-column gap-1"
            >
              <svg viewBox="0 0 100 100" style={{ width: '32px', height: '32px' }}>
                <path d="M50 20 L50 10 M45 15 L55 15 M50 20 C30 20 20 35 20 50 C20 70 30 85 50 85 C70 85 80 70 80 50 C80 35 70 20 50 20 Z" 
                      fill="white" 
                      stroke="black" 
                      strokeWidth="2"/>
              </svg>
              <span className="fw-semibold small">White</span>
            </Button>
            
            <Button
              variant={selectedColor === 'random' ? 'primary' : 'outline-secondary'}
              onClick={() => setSelectedColor('random')}
              aria-label="Random color"
              className="flex-fill p-2 d-flex flex-column gap-1"
            >
              <svg viewBox="0 0 100 100" style={{ width: '32px', height: '32px' }}>
                <path d="M30 50 L70 50 M50 30 L50 70 M35 35 L65 65 M65 35 L35 65" 
                      stroke="currentColor" 
                      strokeWidth="3"
                      fill="none"/>
                <circle cx="50" cy="50" r="35" 
                        fill="none" 
                        stroke="currentColor" 
                        strokeWidth="3"/>
                <text x="50" y="57" 
                      textAnchor="middle" 
                      fontSize="24" 
                      fontWeight="bold"
                      fill="currentColor">?</text>
              </svg>
              <span className="fw-semibold">Random</span>
            </Button>
            
            <Button
              variant={selectedColor === 'black' ? 'primary' : 'outline-secondary'}
              onClick={() => setSelectedColor('black')}
              aria-label="Play as Black"
              className="flex-fill p-2 d-flex flex-column gap-1"
            >
              <svg viewBox="0 0 100 100" style={{ width: '32px', height: '32px' }}>
                <path d="M50 20 L50 10 M45 15 L55 15 M50 20 C30 20 20 35 20 50 C20 70 30 85 50 85 C70 85 80 70 80 50 C80 35 70 20 50 20 Z" 
                      fill="black" 
                      stroke="gray" 
                      strokeWidth="2"/>
              </svg>
              <span className="fw-semibold small">Black</span>
            </Button>
          </div>
        </div>
      </Modal.Body>
      <Modal.Footer>
        <Button variant="secondary" onClick={handleCancel}>
          Cancel
        </Button>
        <Button variant="primary" onClick={handleConfirm}>
          Start Game
        </Button>
      </Modal.Footer>
    </Modal>
  );
};

export default NewGameDialog;