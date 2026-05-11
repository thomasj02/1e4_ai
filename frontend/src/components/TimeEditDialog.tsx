'use client';

import React, { useState, useEffect } from 'react';

interface TimeEditDialogProps {
  isOpen: boolean;
  onClose: () => void;
  onConfirm: (minutes: number, seconds: number) => void;
  currentTime: number; // in milliseconds
  playerName: string;
}

const TimeEditDialog: React.FC<TimeEditDialogProps> = ({
  isOpen,
  onClose,
  onConfirm,
  currentTime,
  playerName
}) => {
  const [minutes, setMinutes] = useState(0);
  const [seconds, setSeconds] = useState(0);

  useEffect(() => {
    if (isOpen) {
      const totalSeconds = Math.floor(currentTime / 1000);
      setMinutes(Math.floor(totalSeconds / 60));
      setSeconds(totalSeconds % 60);
    }
  }, [isOpen, currentTime]);

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    onConfirm(minutes, seconds);
  };

  if (!isOpen) return null;

  return (
    <>
      <div className="modal show d-block" tabIndex={-1}>
        <div className="modal-dialog modal-dialog-centered">
          <div className="modal-content">
            <div className="modal-header">
              <h5 className="modal-title">Edit Time - {playerName}</h5>
              <button
                type="button"
                className="btn-close"
                onClick={onClose}
                aria-label="Close"
              />
            </div>
            <form onSubmit={handleSubmit}>
              <div className="modal-body">
                <div className="row g-3">
                  <div className="col-6">
                    <label htmlFor="minutes" className="form-label">Minutes</label>
                    <input
                      type="number"
                      className="form-control"
                      id="minutes"
                      value={minutes}
                      onChange={(e) => setMinutes(Math.max(0, parseInt(e.target.value) || 0))}
                      min="0"
                      max="99"
                    />
                  </div>
                  <div className="col-6">
                    <label htmlFor="seconds" className="form-label">Seconds</label>
                    <input
                      type="number"
                      className="form-control"
                      id="seconds"
                      value={seconds}
                      onChange={(e) => setSeconds(Math.max(0, Math.min(59, parseInt(e.target.value) || 0)))}
                      min="0"
                      max="59"
                    />
                  </div>
                </div>
              </div>
              <div className="modal-footer">
                <button type="button" className="btn btn-secondary" onClick={onClose}>
                  Cancel
                </button>
                <button type="submit" className="btn btn-primary">
                  Set Time
                </button>
              </div>
            </form>
          </div>
        </div>
      </div>
      <div className="modal-backdrop fade show" onClick={onClose} />
    </>
  );
};

export default TimeEditDialog;