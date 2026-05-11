'use client';

import React, { useEffect, useState } from 'react';
import { Dropdown } from 'react-bootstrap';

const themes = [
  { name: 'light', icon: '☀️' },
  { name: 'dark', icon: '🌙' },
  { name: 'dracula', icon: '🧛' },
  { name: 'night', icon: '🌃' }
];

export default function ThemeSwitcher() {
  const [currentTheme, setCurrentTheme] = useState('dark');

  useEffect(() => {
    // Get saved theme from localStorage or default to dark
    const savedTheme = localStorage.getItem('theme') || 'dark';
    setCurrentTheme(savedTheme);
    document.documentElement.setAttribute('data-theme', savedTheme);
  }, []);

  const handleThemeChange = (theme: string) => {
    setCurrentTheme(theme);
    localStorage.setItem('theme', theme);
    document.documentElement.setAttribute('data-theme', theme);
  };

  return (
    <Dropdown align="end">
      <Dropdown.Toggle variant="ghost" className="rounded-circle p-2" id="theme-dropdown">
        <svg style={{ width: '20px', height: '20px' }} fill="none" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">
          <path d="M12 2.25a.75.75 0 01.75.75v2.25a.75.75 0 01-1.5 0V3a.75.75 0 01.75-.75zM7.5 12a4.5 4.5 0 119 0 4.5 4.5 0 01-9 0zM18.894 6.166a.75.75 0 00-1.06-1.06l-1.591 1.59a.75.75 0 101.06 1.061l1.591-1.59zM21.75 12a.75.75 0 01-.75.75h-2.25a.75.75 0 010-1.5H21a.75.75 0 01.75.75zM17.834 18.894a.75.75 0 001.06-1.06l-1.59-1.591a.75.75 0 10-1.061 1.06l1.59 1.591zM12 18a.75.75 0 01.75.75V21a.75.75 0 01-1.5 0v-2.25A.75.75 0 0112 18zM7.758 17.303a.75.75 0 00-1.061-1.06l-1.591 1.59a.75.75 0 001.06 1.061l1.591-1.59zM6 12a.75.75 0 01-.75.75H3a.75.75 0 010-1.5h2.25A.75.75 0 016 12zM6.697 7.757a.75.75 0 001.06-1.06l-1.59-1.591a.75.75 0 00-1.061 1.06l1.59 1.591z" fill="currentColor"/>
        </svg>
      </Dropdown.Toggle>
      <Dropdown.Menu>
        {themes.map((theme) => (
          <Dropdown.Item
            key={theme.name}
            active={currentTheme === theme.name}
            onClick={() => handleThemeChange(theme.name)}
          >
            <span className="me-2">{theme.icon}</span>
            {theme.name.charAt(0).toUpperCase() + theme.name.slice(1)}
          </Dropdown.Item>
        ))}
      </Dropdown.Menu>
    </Dropdown>
  );
}