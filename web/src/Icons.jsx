const svg = (children, size = 24, extra = {}) => (
  <svg viewBox="0 0 24 24" width={size} height={size} fill="none" stroke="currentColor" stroke-width="1.8"
    stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" {...extra}>
    {children}
  </svg>
);

export const MenuIcon = ({ size }) => svg(<path d="M4 6h16M4 12h16M4 18h16" />, size);
export const BackIcon = ({ size }) => svg(<path d="M15 5l-7 7 7 7" />, size);
export const EyeIcon = ({ size }) => svg(<><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z" /><circle cx="12" cy="12" r="3" /></>, size);
export const EyeOffIcon = ({ size }) => svg(<><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z" /><circle cx="12" cy="12" r="3" /><path d="M4 4l16 16" /></>, size);

export const EnabledIcon = ({ size }) => svg(<><circle cx="12" cy="12" r="9" /><path d="M8 12.5l2.7 2.7L16 9.5" /></>, size);
export const DisabledIcon = ({ size }) => svg(<><circle cx="12" cy="12" r="9" /><path d="M5.6 5.6l12.8 12.8" /></>, size);
export const BackoffIcon = ({ size }) => svg(<><circle cx="12" cy="13" r="8" /><path d="M12 13V9M9 3h6" /></>, size);

export const PumpOffIcon = ({ size }) => svg(<path d="M12 3v8M6.4 6.4a8 8 0 1 0 11.2 0" />, size);
export const PumpOnIcon = ({ size }) => svg(<path d="M12 3s6 6.5 6 11a6 6 0 0 1-12 0c0-4.5 6-11 6-11z" />, size);
export const FaultIcon = ({ size }) => svg(<><path d="M12 3l10 18H2z" /><path d="M12 10v5M12 18h.01" /></>, size);
