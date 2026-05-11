'use client';

import { useEffect } from 'react';

export default function ViewportHeightFix() {
  useEffect(() => {
    // Function to update CSS custom properties with actual measurements
    const updateMeasurements = () => {
      // Get the actual viewport height (accounts for browser chrome)
      const vh = window.innerHeight * 0.01;
      document.documentElement.style.setProperty('--vh', `${vh}px`);
      
      // Use visualViewport if available (better for mobile browsers)
      if (window.visualViewport) {
        const vvh = window.visualViewport.height * 0.01;
        document.documentElement.style.setProperty('--vvh', `${vvh}px`);
        console.log('Visual viewport height:', window.visualViewport.height);
      }
      
      // Measure actual navbar height if it exists
      const navbar = document.querySelector('.navbar');
      if (navbar) {
        const navbarHeight = navbar.getBoundingClientRect().height;
        document.documentElement.style.setProperty('--navbar-height', `${navbarHeight}px`);
        console.log('Actual navbar height:', navbarHeight);
      }
    };

    // Set initial value
    updateMeasurements();
    
    // Also measure after a short delay to ensure navbar is rendered
    setTimeout(updateMeasurements, 100);

    // Update on resize and orientation change
    window.addEventListener('resize', updateMeasurements);
    window.addEventListener('orientationchange', updateMeasurements);

    // Also update when the viewport size changes (e.g., when Safari hides/shows bars)
    const visualViewport = window.visualViewport;
    if (visualViewport) {
      visualViewport.addEventListener('resize', updateMeasurements);
    }

    return () => {
      window.removeEventListener('resize', updateMeasurements);
      window.removeEventListener('orientationchange', updateMeasurements);
      if (visualViewport) {
        visualViewport.removeEventListener('resize', updateMeasurements);
      }
    };
  }, []);

  return null;
}