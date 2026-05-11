// Global type declarations for ChessMimic

// Extend Window interface if needed for global variables
declare global {
  interface Window {
    // Add any global window properties here if needed
    __CHESSMIMIC_VERSION__?: string;
  }
}

// CSS Modules declarations
declare module '*.module.css' {
  const classes: { readonly [key: string]: string };
  export default classes;
}

// Asset declarations
declare module '*.svg' {
  import React from 'react';
  const SVG: React.FC<React.SVGProps<SVGSVGElement>>;
  export default SVG;
}

declare module '*.png' {
  const value: string;
  export default value;
}

declare module '*.jpg' {
  const value: string;
  export default value;
}

declare module '*.jpeg' {
  const value: string;
  export default value;
}

declare module '*.gif' {
  const value: string;
  export default value;
}

// Make sure this file is treated as a module
export {};