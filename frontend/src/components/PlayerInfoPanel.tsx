'use client';

import React from 'react';

interface PlayerInfoPanelProps {
  name: string;
  rating: number;
  time: number;
  isActive: boolean;
  isBot?: boolean;
  onEditTime?: () => void;
  variant?: 'default' | 'mobile';
  timeTestId?: string;
}

const PlayerInfoPanel: React.FC<PlayerInfoPanelProps> = ({
  name,
  rating,
  time,
  isActive,
  isBot = false,
  onEditTime,
  variant = 'default',
  timeTestId
}) => {
  // Format time as mm:ss
  const formatTime = (ms: number) => {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    return `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
  };

  const formattedTime = formatTime(time);
  
  // Determine time warning level based on remaining time
  const getTimeWarningClass = () => {
    if (time < 10000) return 'text-danger'; // Less than 10 seconds
    if (time < 30000) return 'text-warning'; // Less than 30 seconds
    return '';
  };

  const isMobile = variant === 'mobile';
  const padding = isMobile ? 'p-2' : 'p-4';
  const avatarSize = isMobile ? '24px' : '40px';
  const fontSize = isMobile ? 'fs-5' : 'fs-3';
  const gap = isMobile ? 'gap-2' : 'gap-3';
  
  return (
    <div className={`${padding} d-flex align-items-center justify-content-between ${isActive ? 'bg-secondary bg-opacity-50' : 'bg-dark'} border border-secondary rounded`}>
      <div className={`d-flex align-items-center ${gap}`}>
        <div className="rounded-circle bg-primary text-white d-flex align-items-center justify-content-center" style={{ width: avatarSize, height: avatarSize }}>
          <span className={isMobile ? "small" : ""}>{isBot ? '🤖' : name[0]}</span>
        </div>
        <div>
          <div className={`${isMobile ? '' : 'fw-semibold'}`}>
            <span className={isMobile ? 'small' : ''}>{name}</span>
            {isMobile && <span className="small opacity-75 ms-1">({rating})</span>}
          </div>
          {!isMobile && <div className="small opacity-75">({rating})</div>}
        </div>
      </div>
      <div className="d-flex align-items-center gap-2">
        <div
          data-testid={timeTestId}
          className={`${fontSize} font-monospace fw-bold ${isActive ? getTimeWarningClass() : 'opacity-50'}`}
        >
          {formattedTime}
        </div>
        {onEditTime && !isMobile && (
          <button
            className="btn btn-sm btn-link p-0"
            onClick={onEditTime}
            title="Edit time"
            style={{ width: '24px', height: '24px' }}
          >
            <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor">
              <path d="M15.502 1.94a.5.5 0 0 1 0 .706L14.459 3.69l-2-2L13.502.646a.5.5 0 0 1 .707 0l1.293 1.293zm-1.75 2.456-10.29 10.29a.5.5 0 0 1-.168.11l-3.25 1.25a.5.5 0 0 1-.65-.65l1.25-3.25a.5.5 0 0 1 .11-.168l10.29-10.29 2.708 2.708z"/>
            </svg>
          </button>
        )}
      </div>
    </div>
  );
};

export default PlayerInfoPanel;