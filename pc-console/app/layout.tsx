import type { Metadata } from 'next';
import { Geist, Geist_Mono } from 'next/font/google';
import './globals.css';

const geistSans = Geist({
  variable: '--font-geist-sans',
  subsets: ['latin'],
});

const geistMono = Geist_Mono({
  variable: '--font-geist-mono',
  subsets: ['latin'],
});

export const metadata: Metadata = {
  metadataBase: new URL(
    process.env.NEXT_PUBLIC_SITE_ORIGIN ?? 'http://localhost:3000',
  ),
  title: 'AEROLINK · M350 任务控制台',
  description: '用于 PC 与 Jetson AGX 通信的 M350 飞行、遥测与视频控制台。',
  openGraph: {
    title: 'AEROLINK · M350 任务控制台',
    description: 'PC ↔ AGX 飞行、遥测与视频控制台。',
    type: 'website',
    images: [
      {
        url: '/og.png',
        width: 1672,
        height: 941,
        alt: 'AEROLINK M350 PC 任务控制台',
      },
    ],
  },
  twitter: {
    card: 'summary_large_image',
    title: 'AEROLINK · M350 任务控制台',
    description: 'PC ↔ AGX 飞行、遥测与视频控制台。',
    images: ['/og.png'],
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="zh-CN">
      <body
        className={`${geistSans.variable} ${geistMono.variable} antialiased`}
      >
        {children}
      </body>
    </html>
  );
}
