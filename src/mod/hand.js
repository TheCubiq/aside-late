// The pointing hand on a stick, shared by the thank-you page and the GitHub and X pages it opens. It's a classic script, so it can
// be injected into another page as well as loaded by ours, and all it adds is asideLateHand. It never takes a click.

(() => {
  if (globalThis.asideLateHand) return;

  // Two poses from one sticker sheet, pointing and waving, each turned upright onto the same stick. The fingertip is at (100, 10)
  // of the drawing, and the hand turns the whole thing around it. The waving one swings from the wrist.
  const MARKUP = `<svg width="200" height="3100" viewBox="-100 -10 200 3100">
  <defs>
    <linearGradient id="stick" x1="0" x2="1">
      <stop offset="0" stop-color="#3a3a3e"/>
      <stop offset="0.45" stop-color="#1c1c1f"/>
      <stop offset="1" stop-color="#0d0d0f"/>
    </linearGradient>
  </defs>
  <rect x="-2.4" y="140.3" width="20" height="3000" rx="10" fill="url(#stick)"/>
  <g data-pose="point" fill="#1d1d21" transform="rotate(19.8) translate(-442.6 -5.4)"><path d="M435.2 8.3C436.3 7.5 438.1 6.6 439.2 6.1C440.5 5.6 442.6 5.4 445.3 5.5C448.9 5.6 449.7 5.8 451.9 7.2C453.3 8 455.2 9.8 456.3 11.2C457.3 12.6 458.5 15 458.9 16.5C459.3 18 460.3 22.9 461 27.4C461.7 31.8 462.6 37 463 38.8C463.4 40.6 463.8 42.1 464 42.2C464.1 42.3 465.3 42 466.7 41.5C468 41.1 470.5 40.6 472.4 40.5C474.3 40.3 476.8 40.4 478.4 40.8C480 41.2 481.9 41.8 482.7 42.2C484.1 42.9 484.6 42.8 487 42.1C488.6 41.7 491 41.3 492.4 41.3C493.7 41.3 496 41.6 497.4 42C498.8 42.4 501.3 43.8 503 45C504.7 46.2 507 47.5 508.3 47.8C509.6 48.2 511.6 49 512.9 49.7C514.1 50.3 516.3 52 517.7 53.5C519.1 55 521.1 57.7 522.1 59.5C523.2 61.5 524.4 64.9 525.2 68.1C526.1 71.6 526.6 75.6 526.8 79.5C527 83.9 526.9 87.1 526.3 90.9C525.9 93.8 524.8 98 524.1 100.3C523.3 102.5 522.1 105.5 520.2 109.5L522.2 111.7C523.3 112.9 525 115.6 525.9 117.7C527.5 121 527.7 121.9 527.4 124.3C527.3 125.8 526.8 127.9 526.3 128.9C525.8 129.8 524.7 131.5 523.8 132.6C522.9 133.7 520.4 136 518.2 137.8C515.9 139.6 512.2 142 509.8 143.3C507.5 144.5 503.8 146.1 501.7 147C499.6 147.8 495.9 148.9 493.4 149.5C490.9 150.1 487 150.7 484.8 150.9C482.3 151.1 479.3 151 477.2 150.6C475.2 150.3 472.5 149.5 471.2 148.8C469.9 148.1 468 146.6 467.1 145.4C466.1 144.2 464.9 142.4 464.5 141.3C463.9 139.9 463.5 137.4 463.1 128.6L459.3 126.9C457.1 126 452.9 123.6 449.8 121.5C446.7 119.4 441.5 115.2 437.9 111.8C434.4 108.5 429.7 104.2 427.3 102.3C425 100.3 422.3 97.5 421.3 96C420.3 94.4 419.1 92.2 418.7 90.9C418.3 89.5 418.2 87 418.3 84.6C418.4 81.7 418.8 79.9 419.6 78.3C420.3 77 421.5 75.2 422.4 74.3C423.3 73.4 425 72 426.3 71.3C428.3 70.1 429.2 69.9 439.2 69.5L437.2 64.6C436.2 62 434.1 56.1 432.6 51.7C431.1 47.2 429.5 41.7 429 39.3C428.3 36.2 428.1 32.8 428.1 27.1C428.1 20.2 428.2 19 429.2 16.5C429.8 15 431 12.8 431.8 11.7C432.6 10.6 434.1 9.1 435.2 8.3Z"/><path fill="#f6f6f8" d="M438.3 12.1C439.1 11.6 440.5 10.9 441.5 10.6C442.4 10.3 443.7 10 444.3 10C444.8 10 446.2 10.2 447.3 10.5C448.4 10.8 450.1 11.9 451.1 12.9C452.1 13.9 453.3 15.9 453.8 17.3C454.3 18.7 455.2 23.3 455.9 27.6C456.6 32 457.9 38.2 458.7 41.4C459.5 44.7 460.8 48.9 461.6 50.8C462.3 52.7 463.8 55.5 464.9 57.1C465.9 58.7 467.3 60.3 467.9 60.6C468.6 60.9 469.2 60.9 469.5 60.7C469.7 60.5 469.2 58.8 468.3 57C467.4 55.2 466.2 52.5 465.7 51.1C464.8 48.5 464.8 48.4 465.6 47.7C466.1 47.3 467.4 46.5 468.6 46.1C469.8 45.5 471.9 45.2 473.9 45.2C476.3 45.2 477.9 45.5 479.9 46.4C481.5 47.2 484.4 49.3 486.7 51.3C488.8 53.3 490.8 54.9 491.2 54.8C491.5 54.8 491.8 54.5 491.9 54.2C492.1 53.9 491.1 52.3 489.8 50.5C488.6 48.8 487.5 47.3 487.5 47.1C487.5 47 488.5 46.6 489.7 46.3C490.9 46 492.8 45.9 493.9 46.1C495 46.3 496.8 47 497.9 47.5C499 48.1 501.3 49.9 503 51.5C504.7 53.1 506.2 54.4 506.4 54.5C506.6 54.5 507 54 507.2 53.4C507.5 52.5 507.6 52.5 510.3 53.8C512.1 54.8 513.7 56.1 514.9 57.8C515.9 59.2 517.2 61.2 517.8 62.3C518.3 63.4 519.3 65.8 519.8 67.6C520.4 69.4 521.1 72.6 521.4 74.6C521.7 76.6 522 80 522 82C522 84.1 521.7 87.6 521.4 89.8C521.1 91.9 520.3 95.5 519.6 97.7C518.9 100 517.4 103.6 516.2 105.8C514.8 108.5 513 111 510.8 113.2C509 115 506.3 117.3 504.8 118.3C503.2 119.3 500.9 120.6 499.7 121.3C498.4 121.9 495.1 123.2 492.2 124.1C489.4 125.1 485 126.1 482.6 126.4C479.5 126.7 477 126.7 474.3 126.4C472.1 126.1 468.9 125.3 467.2 124.7C465.4 124.1 462.2 122.8 460.1 121.7C457.9 120.6 454.8 118.9 453.2 117.9C451.6 116.8 448.9 114.8 447 113.3C445.2 111.7 441.9 108.8 439.7 106.6C437.5 104.5 433.8 101.2 431.6 99.4C429.4 97.5 427.1 95.4 426.5 94.6C425.9 93.8 425 92.3 424.4 91.2C423.8 90 423.2 88.2 423 87.1C422.8 85.9 422.9 84 423.3 82.6C423.8 80.7 424.6 79.4 426.3 77.7C427.7 76.2 429.3 75.1 430.6 74.8C431.7 74.5 433.5 74.2 434.5 74.2C435.6 74.2 437.5 74.6 438.8 75C440.1 75.5 441.8 76.2 442.5 76.7C443.2 77.2 444.4 79.2 445.5 81.6C446.4 83.8 447.4 85.7 447.7 85.9C448 86.1 448.5 86.1 448.8 85.9C449 85.7 449.1 84.5 448.9 83.1C448.7 81.7 448 78.9 447.4 77C446.7 75 444.6 69.6 442.7 64.8C440.9 60.1 438.4 53.5 437.3 50.2C436.2 46.8 434.8 41.4 434.1 38C433.3 34.2 432.9 30.2 432.9 27.3C432.9 24.7 433.1 21.5 433.4 20.2C433.6 18.9 434.6 16.7 435.4 15.4C436.2 14.1 437.5 12.6 438.3 12.1Z"/><path fill="#f6f6f8" d="M505 121.8C507.1 120.6 510.4 118.1 512.3 116.4C514.1 114.6 515.9 113.2 516.1 113.2C516.4 113.2 517.5 114.2 518.6 115.3C519.6 116.5 520.9 118.4 521.5 119.5C522 120.6 522.5 122.4 522.5 123.5C522.5 125 522 126.3 520.8 128.1C519.9 129.5 518 131.6 516.6 132.7C515.2 133.8 513 135.5 511.6 136.5C510.2 137.4 507 139.1 504.5 140.3C502 141.5 497.9 143.1 495.4 143.8C492.9 144.5 489.4 145.3 487.5 145.6C485.7 145.8 483 146.1 481.5 146.1C479.9 146.1 477.5 145.7 476 145.3C474.5 144.9 472.6 143.8 471.6 142.9C470.3 141.8 469.5 140.4 469 138.7C468.6 137.3 468.3 134.8 468.3 133.2C468.3 131.5 468.5 129.8 468.7 129.5C469 129 469.8 129 472.9 129.4C475.1 129.8 478.4 129.8 481 129.6C483.3 129.4 487.3 128.7 489.8 128.1C492.3 127.4 495.9 126.3 497.7 125.5C499.5 124.8 502.8 123.1 505 121.8Z"/><path d="M501.1 71.3C501.1 70.9 501.4 70.1 502 69.6C502.5 69.1 503.3 68.6 503.8 68.6C504.3 68.6 505.3 69.2 505.9 69.8C506.5 70.5 507.7 72.8 508.4 75C509.2 77.2 510.1 80.5 510.6 82.3C511 84.1 511.3 86.6 511.3 87.9C511.3 89.3 511 90.5 510.4 91C510 91.5 509.2 91.9 508.7 91.9C508.2 91.9 507.5 91.6 507.2 91.3C506.8 90.9 506 88.3 505.3 85.6C504.7 82.8 503.5 78.7 502.7 76.4C501.9 74.1 501.2 71.8 501.1 71.3Z"/><path d="M488.4 74.5C488.6 73.8 489.1 73 489.6 72.7C490 72.4 490.6 72.2 490.8 72.2C491.1 72.2 491.9 72.6 492.6 73.1C493.3 73.6 494.6 75.7 495.5 77.8C496.4 79.9 497.7 83.7 498.4 86.4C499.1 89 499.7 92 499.7 92.9C499.7 93.9 499.4 95.1 499.1 95.5C498.7 96 498.1 96.4 497.7 96.6C497.2 96.7 496.4 96.6 495.8 96.4C495 96.1 494.5 95.3 494.1 93.8C493.8 92.6 492.9 89.4 492 86.6C491.2 83.8 489.9 80.2 489.3 78.6C488.4 76.7 488.2 75.3 488.4 74.5Z"/><path d="M475.5 82.8C474.7 80.5 474.7 80.4 475.5 79.1C476.1 78.2 476.9 77.7 477.8 77.7C478.6 77.7 479.6 78.1 480.1 78.6C480.5 79.1 481.7 81.5 482.7 83.9C483.7 86.4 485.1 90.4 485.8 92.8C486.5 95.2 487 97.8 487 98.5C487 99.2 486.6 100.2 486.1 100.7C485.7 101.2 484.8 101.6 484.2 101.6C483.7 101.6 482.9 101.3 482.4 100.9C481.9 100.6 480.4 96.9 479 92.7C477.6 88.5 476 84 475.5 82.8Z"/></g>
  <g data-pose="wave" fill="#1d1d21" style="display:none" data-wrist="7.6 150.3 19.4 -372.2 -319.4" transform="translate(7.6 150.3) rotate(19.4) translate(-372.2 -319.4)"><path d="M323 187.4C324 186.8 325.8 186 327.1 185.7C328.3 185.3 330.5 185.2 332 185.3C333.5 185.4 335.6 186 336.8 186.6C338 187.2 340.2 188.9 341.7 190.3C343.5 192.1 345.1 194.3 346.6 197.2C347.9 199.8 348.8 201.2 349 200.7C349.1 200.3 349.7 198.4 350.3 196.4C350.9 194.3 352.1 191.9 353.2 190.3C354.5 188.7 356.1 187.4 357.7 186.6C359.5 185.6 361.2 185.3 364 185.1C367.3 185 368.2 185.1 370.3 186.2C371.7 186.9 373.5 188.1 374.3 188.8C375 189.5 376.2 190.9 376.9 192C377.6 193 378.6 195.5 379.2 197.4C379.8 199.4 380.5 202.3 380.7 204C381 205.7 381.6 211.6 382 217.2C382.4 222.7 382.8 227.3 382.9 227.4C382.9 227.5 384 226.8 385.3 225.8C386.5 224.9 388.8 223.6 390.3 222.9C392.3 222.1 394.2 221.8 396.9 221.8C400 221.7 401.2 222 403 222.9C404.2 223.6 406.1 224.7 407 225.4C408 226.2 409.3 228 410.2 229.8C411.3 232.3 411.6 233.6 411.6 236.5C411.6 238.9 411.2 241.2 410.6 243.1C410 244.7 408.9 246.8 408.1 247.8C407.2 248.8 405.2 251.4 403.5 253.6C401.8 255.8 399.6 259.1 398.7 260.9C397.8 262.7 396.5 265.8 395.8 267.8C395.1 269.7 393.9 272.5 391.9 276.6L392.8 278.3C393.4 279.2 394.2 281.2 394.7 282.7C395.2 284.2 396.3 286.6 397.1 288C397.9 289.4 399.2 292.2 400 294.2C400.8 296.2 401.5 298.7 401.5 299.8C401.5 300.8 401 302.7 400.3 303.9C399.7 305.2 398 307.2 396.4 308.5C394.9 309.8 391.9 311.8 389.8 313.1C387.7 314.3 383 316.6 379.2 318.2C375.4 319.8 369.4 321.9 365.8 322.9C362.2 323.9 357.2 325.1 354.6 325.5C352.1 325.9 348.5 326.2 346.5 326.2C344.6 326.2 342.2 326 341.2 325.7C340.3 325.4 338.8 324.7 337.9 324.1C337.1 323.6 335.9 322.4 335.3 321.5C334.7 320.6 333.8 318.8 333.4 317.4C333 316 332.4 312.7 332.1 310C331.8 307.4 331.2 304 330.8 302.4C330.4 300.9 330.1 299 330.1 298.1C330.1 296.8 329.6 296.3 325.7 293.8C323.3 292.3 318.9 288.7 316 285.9C310.6 280.8 310.6 280.7 301.7 276.6C296.9 274.4 291.9 271.9 290.6 271.1C289.4 270.3 287.2 268.5 285.9 267.1C284.6 265.6 283 263.4 282.4 262.1C281.6 260.4 281.3 258.7 281.3 256.1C281.3 253.4 281.6 252 282.5 250.1C283.2 248.7 284.7 246.8 285.8 245.9C286.9 245 288.6 243.8 289.6 243.4C290.6 242.9 292.6 242.5 294.1 242.3C295.8 242.2 298.4 242.5 300.3 242.9C302.2 243.4 303.9 243.7 304 243.7C304.2 243.7 302.7 241.3 300.7 238.3C298.7 235.3 296.5 231.5 295.7 229.8C294.9 228.1 293.8 225.4 293.2 223.6C292.5 221.6 292.1 219.1 292.1 216.8C292.1 214.8 292.5 212.1 292.9 210.8C293.3 209.6 294.6 207.4 295.8 206.1C297 204.7 298.9 203.2 300 202.6C301.1 202 303.4 201.5 305 201.3C306.8 201.2 308.9 201.4 310.1 201.7C311.2 202.1 313 202.9 314 203.6C315.1 204.2 316 204.8 316 204.8C316.1 204.8 316.2 202.9 316.2 200.6C316.2 198.2 316.6 195.7 317 194.6C317.4 193.7 318.5 191.9 319.5 190.7C320.4 189.5 322 188 323 187.4Z"/><path fill="#f6f6f8" d="M358.1 192.3C359 191.4 360.4 190.5 361.2 190.2C362.1 189.9 363.5 189.7 364.4 189.6C365.3 189.6 366.8 189.9 367.7 190.3C368.6 190.6 370.1 191.8 371.1 192.9C372.1 194 373.4 196.2 374 197.7C374.6 199.2 375.4 202.9 375.9 205.8C376.3 208.7 377 217 377.4 224.1C377.8 231.3 378.4 237.7 378.7 238.4C379 239.2 379.5 239.6 380.1 239.6C380.5 239.5 381.5 238.3 382.2 236.9C382.9 235.5 384.6 233.2 386 231.8C387.6 230.2 389.8 228.6 391.6 227.8C394.1 226.7 395.2 226.4 397.5 226.6C399.4 226.7 400.9 227.2 402.4 228.2C403.6 229 405 230.5 405.6 231.7C406.3 233.1 406.7 234.8 406.7 236.7C406.7 238.7 406.3 240.2 405.3 242.2C404.6 243.7 402.6 246.6 401 248.5C399.4 250.5 397.3 253.3 396.2 254.9C395.2 256.4 393.2 260.6 391.8 264.2C390.4 267.9 388.5 272.1 387.6 273.6C386.8 275.1 385.1 277 384 277.8C382.9 278.6 379 280.7 375.4 282.5C371.8 284.2 365.7 286.7 362 287.9C358.2 289.2 352.5 290.8 349.3 291.5C346.1 292.2 341.7 292.9 339.5 293.1C335.5 293.4 335.3 293.4 331.9 291.7C329.9 290.7 326.6 288.5 324.5 286.9C322.4 285.2 319.2 282.3 317.4 280.3C314.5 277.2 313.5 276.6 308.1 274.2C304.7 272.7 300.2 270.6 297.9 269.5C295.7 268.5 292.7 266.5 291.2 265.2C289.7 263.8 288 261.8 287.4 260.7C286.8 259.4 286.4 257.7 286.4 256.1C286.4 254.7 286.8 252.9 287.2 252C287.7 251.1 288.9 249.7 289.9 248.9C291.6 247.7 292.2 247.5 295.9 247.5C299.3 247.5 300.6 247.8 303.8 249.1C305.9 250 309.4 251.7 311.7 253C314 254.3 316.3 255.3 316.8 255.2C317.3 255.2 317.7 254.8 317.8 254.4C317.9 253.9 315.8 250.6 313.1 247C310.4 243.4 306.7 238.3 305 235.6C303.2 233 300.9 228.9 299.8 226.5C298.7 224.2 297.5 220.7 297.2 218.9C296.9 217.1 296.8 215 297 214.1C297.1 213.3 297.8 211.7 298.4 210.6C299.1 209.5 300.3 208.1 301.3 207.5C302.2 206.9 303.7 206.2 304.5 206.1C305.4 205.9 307 205.9 308.1 206.2C309.2 206.5 311.1 207.5 312.4 208.5C313.6 209.4 315.8 211.9 317.2 213.9C318.7 216 322.4 222.2 325.6 227.8C328.8 233.4 331.8 238.5 332.4 239.1C333 239.9 333.7 240.2 334.3 240C334.9 239.8 335.1 239.3 335 238.6C334.9 237.9 332.5 232.6 329.6 226.8C326.8 220.9 323.8 214 323 211.3C321.9 208.2 321.3 205.1 321.1 202.2C320.9 198.4 321 197.7 322.1 195.7C322.7 194.5 324.1 192.8 325 192C326.3 191 327.6 190.4 329.3 190.2C330.7 190.1 332.7 190.2 333.8 190.5C334.9 190.9 336.6 192.1 338 193.5C339.4 195 341.2 197.7 342.5 200.5C343.8 203 346.7 210.9 349.1 218.2C351.5 225.4 353.9 231.7 354.3 232.2C354.9 232.9 355.5 233 356.1 232.7C356.8 232.4 357 232 356.7 230.8C356.5 230 355.9 226.2 355.3 222.5C354.6 217.7 354.2 213.1 354.2 207.3C354.1 199.7 354.2 198.7 355.3 196.4C355.9 195 357.2 193.2 358.1 192.3Z"/><path fill="#f6f6f8" d="M373.4 286.8C376.4 285.4 380.7 283.3 382.7 282.1C384.8 280.8 386.8 279.7 387.2 279.7C387.5 279.7 388.2 280.4 388.6 281.3C389.1 282.2 389.7 283.6 389.9 284.3C390.3 285.6 390.1 285.9 388.4 287.4C387.3 288.3 384.4 290.1 381.9 291.4C379.5 292.8 375.1 294.8 372.4 295.9C369.6 297.1 364.8 298.8 361.7 299.7C358.7 300.7 353.9 301.9 351.1 302.4C348.3 302.9 344.1 303.6 341.7 303.8C337.6 304.1 337.4 304.1 336.5 302.9C336 302.2 335.6 300.6 335.5 299.1C335.4 296.7 335.4 296.6 336.9 296.5C337.8 296.4 340.2 296.1 342.2 295.8C344.3 295.5 348.4 294.7 351.2 294C354.1 293.4 359 292 362.1 291C365.2 290 370.3 288.1 373.4 286.8Z"/><path fill="#f6f6f8" d="M379.5 295.9C382.8 294.3 386.8 292 388.3 290.9C389.8 289.7 391.3 288.9 391.6 289C391.9 289 393 290.8 394 292.9C395 294.9 396 297.4 396.2 298.4C396.4 299.6 396.3 300.7 395.7 301.8C395.3 302.7 393.5 304.5 391.7 305.8C390 307.1 385.7 309.5 382.2 311.2C378.8 313 373.1 315.3 369.6 316.5C366.1 317.6 361.1 319.1 358.4 319.7C355.8 320.3 351.4 320.9 348.6 321C344.8 321.3 343.1 321.2 341.8 320.6C340.7 320.2 339.7 319.2 339.1 318C338.6 317 337.9 314.5 337.4 312.6C337 310.6 336.7 308.5 336.7 307.9C336.7 306.8 336.9 306.7 342.6 306.4C345.9 306.2 351 305.5 353.9 304.8C356.8 304.1 362.4 302.5 366.3 301.2C370.2 299.9 376.1 297.5 379.5 295.9Z"/></g>
</svg>`;
  const SVG = "http://www.w3.org/2000/svg";
  const TAU = Math.PI * 2;
  const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
  const easeInOut = t => (t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2);

  class Interrupted extends Error {}

  // A wobbly loop around a box, a bit more than once round.
  function loopAround(box, seed) {
    const cx = box.left + box.width / 2, cy = box.top + box.height / 2;
    const rx = box.width / 2 + 16, ry = box.height / 2 + 14;
    const turns = 1.18, steps = 64, start = -2.3 + seed;
    const points = [];
    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      const theta = start + t * turns * TAU;
      const wobble = 1 + Math.sin(theta * 3 + seed * 5) * 0.05 + t * 0.07;
      points.push({ x: cx + Math.cos(theta) * rx * wobble, y: cy + Math.sin(theta) * ry * wobble - t * 4 });
    }
    return points;
  }

  // The look and feel, all in one place. Each create() gets its own copy, so a page can change it while the hand runs.
  const STYLE = {
    // Where someone off screen holds the stick: the angle it comes in at, in degrees from straight up, and how far from the middle
    // of the screen they stand. Lag is how far the hand trails behind the held end when it moves.
    hold: { angle: -36, reach: 1250, lag: 0.15 },
    // How the hand gets where it's going: how quickly, and how much it overshoots and wobbles on arrival, from 0 to 1.
    move: { speed: 230, bounce: 0.1 },
    // Where the fingertip lands on a button, as a share of its width and height, and where the hand waits to wave, in pixels
    // from that spot.
    aim: { x: 0.51, y: 0.55, waveX: 34, waveY: -20 },
    // The wave: degrees the wrist swings, waves a second, how long it lingers at each end from 0 to 1, degrees the whole stick
    // swings with it, how far the wrist trails the stick in degrees of the wave, and seconds to start and settle.
    wave: { swing: 8, speed: 3.6, linger: 0.65, stick: 1.5, delay: 75, ease: 0.06 },
    // Switching between pointing and waving: degrees the hand flicks at the wrist, how long that takes, and how much of it the
    // two pictures fade over, from 0 for a clean cut.
    swap: { turn: 25, ms: 160, fade: 0 },
    // The lines on a tap: how many, degrees between them, how big, how thick, and how long they last.
    tap: { rays: 5, spread: 36, size: 1, width: 3.5, ms: 320 },
    // It never holds still: a lazy wander in pixels, how lively that is, and degrees the stick sways from the held end. Drawing a loop calms
    // it, so the ink still comes out of the fingertip.
    idle: { wander: 5, pace: 1.8, rock: 0.6 },
  };

  // Puts the hand and its ink into `layer`.
  function create({ layer, ink = "#1d9bf0", size = 1.25, markup = MARKUP }) {
    const style = JSON.parse(JSON.stringify(STYLE));
    const hand = document.createElement("div");
    hand.setAttribute("aria-hidden", "true");
    Object.assign(hand.style, { position: "fixed", left: "0", top: "0", pointerEvents: "none", willChange: "transform", zIndex: "2147483647" });
    hand.innerHTML = markup;
    // The stick goes in a picture of its own behind the hand. It's thousands of pixels long, and with it in the same picture, every
    // change to the hand redrew all of it, which could leave a frame blank when the poses switched.
    const art = hand.firstElementChild, stick = art.cloneNode(false);
    stick.append(art.querySelector("defs"), art.querySelector("rect"));
    art.setAttribute("height", "300");
    art.setAttribute("viewBox", "-100 -10 200 300");
    hand.prepend(stick);
    for (const picture of [stick, art]) Object.assign(picture.style, { position: "absolute", left: "-100px", top: "-10px", overflow: "visible", filter: "drop-shadow(0 6px 10px rgb(0 0 0 / 0.35))" });
    const pointing = hand.querySelector('[data-pose="point"]'), waving = hand.querySelector('[data-pose="wave"]');
    const [wristX, wristY, upright, backX, backY] = waving.dataset.wrist.split(" ").map(Number);
    // The pointing pose sits in a group of its own, so it can turn at the wrist while it hands over to the waving one.
    const pointingTurn = document.createElementNS(SVG, "g");
    pointing.replaceWith(pointingTurn);
    pointingTurn.append(pointing);
    waving.style.display = "";
    waving.setAttribute("opacity", "0");

    const paper = document.createElementNS(SVG, "svg");
    paper.setAttribute("aria-hidden", "true");
    Object.assign(paper.style, { position: "fixed", inset: "0", width: "100%", height: "100%", pointerEvents: "none", overflow: "visible", zIndex: "2147483646" });
    const scribble = document.createElementNS(SVG, "path");
    for (const [name, value] of Object.entries({ fill: "none", stroke: ink, "stroke-width": "4", "stroke-linecap": "round", "stroke-linejoin": "round", opacity: "0" })) scribble.setAttribute(name, value);
    paper.append(scribble);
    layer.append(paper, hand);

    // Where the stick is held. It drifts a little with the hand, so the stick doesn't swing all the way round on a wide screen.
    const holder = (x, y) => {
      const r = (style.hold.angle * Math.PI) / 180, cx = innerWidth / 2, cy = innerHeight / 2;
      return { x: cx - Math.sin(r) * style.hold.reach + (x - cx) * 0.15, y: cy + Math.cos(r) * style.hold.reach + (y - cy) * 0.15 };
    };
    const held = (x, y) => {
      const from = holder(x, y);
      return (Math.atan2(-(from.x - x), from.y - y) * 180) / Math.PI;
    };
    // Turns the hand around the held end of the stick, which is how a hand on a stick moves when someone swings it.
    const swingAround = (point, from, degrees) => {
      const r = (degrees * Math.PI) / 180, dx = point.x - from.x, dy = point.y - from.y;
      return { x: from.x + dx * Math.cos(r) - dy * Math.sin(r), y: from.y + dx * Math.sin(r) + dy * Math.cos(r) };
    };

    const at = { x: innerWidth + 160, y: innerHeight + 160 };
    // Off screen until it starts moving. Otherwise it waits in the top left corner, wherever the page puts a new element.
    hand.style.transform = `translate(${at.x}px, ${at.y}px) rotate(${held(at.x, at.y)}deg) scale(${size})`;
    const speed = { x: 0, y: 0 };
    let goal = { ...at };
    let wave = 0, waveEnv = 0, wavePhase = 0, trailing = 1, pose = 0, swapped = 0, shaking = 0;
    let tap = 0, shake = 0, drawing = false, last = 0, running = false, generation = 0;
    // The hand's own clock, in milliseconds. Everything it does runs on it, so slowing it down or stopping it holds the whole thing.
    let clock = 0, timeScale = 1;
    const waits = new Set();
    const tip = { x: at.x, y: at.y, angle: 0 };

    // A little fan of lines bursting out of the fingertip, like a cartoon tap.
    function burst() {
      const group = document.createElementNS(SVG, "g");
      const { rays: count, spread, size: scale, width, ms } = style.tap;
      const rays = Array.from({ length: count }, (_, i) => {
        const line = document.createElementNS(SVG, "line");
        for (const [name, value] of Object.entries({ stroke: ink, "stroke-width": String(width), "stroke-linecap": "round" })) line.setAttribute(name, value);
        group.append(line);
        return { line, rad: ((tip.angle + (i - (count - 1) / 2) * spread) * Math.PI) / 180 };
      });
      paper.append(group);
      const { x, y } = tip, began = clock;
      const step = () => {
        const t = Math.min((clock - began) / ms, 1), eased = 1 - (1 - t) ** 3;
        const from = (13 + 9 * eased) * scale, to = from + 9 * scale * (1 - t * 0.6);
        for (const { line, rad } of rays) {
          const dx = Math.sin(rad), dy = -Math.cos(rad);
          line.setAttribute("x1", x + dx * from);
          line.setAttribute("y1", y + dy * from);
          line.setAttribute("x2", x + dx * to);
          line.setAttribute("y2", y + dy * to);
        }
        group.setAttribute("opacity", String(t < 0.5 ? 1 : 1 - (t - 0.5) * 2));
        if (t < 1 && running) requestAnimationFrame(step);
        else group.remove();
      };
      requestAnimationFrame(step);
    }

    let inkFade = null;
    function frame(now) {
      if (!running) return;
      const dt = Math.min((now - last) / 1000, 1 / 30) * timeScale;
      last = now;
      clock += dt * 1000;
      for (const wait of waits) {
        if (clock >= wait.until) {
          waits.delete(wait);
          wait.resolve();
        }
      }
      if (inkFade) {
        const t = Math.min((clock - inkFade.began) / inkFade.ms, 1);
        scribble.setAttribute("opacity", String(inkFade.from * (1 - t)));
        if (t >= 1) inkFade = null;
      }

      // A spring that's a little underdamped, so the hand overshoots and wobbles like someone's holding it. Drawing a loop uses a
      // tight one, so the fingertip stays on the ink.
      const pull = drawing ? 900 : style.move.speed;
      const damping = 2 * Math.sqrt(pull) * (drawing ? 0.72 : 1 - clamp(style.move.bounce, 0, 1) * 0.8);
      for (const axis of ["x", "y"]) {
        speed[axis] += (pull * (goal[axis] - at[axis]) - damping * speed[axis]) * dt;
        at[axis] += speed[axis] * dt;
      }
      const w = style.wave;
      waveEnv += (wave - waveEnv) * (1 - Math.exp(-dt / Math.max(w.ease, 0.01)));
      wavePhase += dt * w.speed * TAU;
      const power = 1.6 - 1.4 * clamp(w.linger, 0, 1);
      const shaped = phase => Math.sign(Math.sin(phase)) * Math.abs(Math.sin(phase)) ** power;
      // The held end leads and the hand trails behind it, so moving right leaves the hand leaning back to the left. Waving the
      // stick swings it from the same end. Not while drawing a loop, where the ink has to come out of the fingertip.
      trailing += ((drawing ? 0 : 1) - trailing) * (1 - Math.exp(-dt / 0.15));
      const trail = -clamp(speed.x * 0.018 - speed.y * 0.006, -14, 14) * style.hold.lag * trailing;
      const stickSwing = shaped(wavePhase) * w.stick * waveEnv;
      // A few slow waves on top of each other, so the wander never quite repeats.
      const t = (clock / 1000) * style.idle.pace, alive = trailing;
      const wanderX = (Math.sin(t * 1.3) * 0.6 + Math.sin(t * 2.9 + 1.7) * 0.3 + Math.sin(t * 5.3 + 0.4) * 0.1) * style.idle.wander * alive;
      const wanderY = (Math.sin(t * 1.7 + 2.1) * 0.6 + Math.sin(t * 3.7 + 0.9) * 0.3 + Math.sin(t * 6.1 + 2.6) * 0.1) * style.idle.wander * alive;
      const rock = (Math.sin(t * 2.3 + 0.5) * 0.7 + Math.sin(t * 4.1 + 1.3) * 0.3) * style.idle.rock * alive;
      const swung = swingAround({ x: at.x + wanderX, y: at.y + wanderY }, holder(at.x, at.y), trail + stickSwing + rock);
      shaking += (shake - shaking) * (1 - Math.exp(-dt / 0.08));
      const nope = Math.sin(clock / 45) * 12 * shaking;
      const angle = held(at.x, at.y) + trail + stickSwing + rock + nope;
      // A tap pushes the finger in along the way it points.
      const rad = (angle * Math.PI) / 180, push = tap * 10;
      const x = swung.x + Math.sin(rad) * push, y = swung.y - Math.cos(rad) * push;
      Object.assign(tip, { x, y, angle });

      // Switching poses flicks the hand out at the wrist and back in as the other one, so it turns one way and back without a jump.
      const step = (dt * 1000) / Math.max(style.swap.ms, 1);
      swapped = pose > swapped ? Math.min(pose, swapped + step) : Math.max(pose, swapped - step);
      const flip = easeInOut(swapped);
      const fade = style.swap.fade, shown = fade > 0 ? clamp((flip - 0.5) / fade + 0.5, 0, 1) : Number(flip >= 0.5);
      const flick = (flip < 0.5 ? flip : 1 - flip) * style.swap.turn;
      pointingTurn.setAttribute("opacity", String(1 - shown));
      pointingTurn.setAttribute("transform", `rotate(${flick} ${wristX} ${wristY})`);
      waving.setAttribute("opacity", String(shown));
      if (flip > 0) {
        const wrist = upright + flick + shaped(wavePhase - (w.delay * Math.PI) / 180) * w.swing * waveEnv;
        waving.setAttribute("transform", `translate(${wristX} ${wristY}) rotate(${wrist}) translate(${backX} ${backY})`);
      }
      hand.style.transform = `translate(${x}px, ${y}px) rotate(${angle}deg) scale(${size * (1 - tap * 0.07)})`;
      requestAnimationFrame(frame);
    }

    const api = {
      Interrupted,
      style,
      get generation() { return generation; },
      get tip() { return { ...tip }; },
      // 1 is normal speed, 0.5 half, 0 holds everything where it is.
      get timeScale() { return timeScale; },
      set timeScale(value) { timeScale = Math.max(0, value); },
      // Makes every wait that's still pending give up, so whoever is driving the hand can start over.
      interrupt() {
        generation++;
        for (const wait of waits) wait.reject(new Interrupted());
        waits.clear();
      },
      start() {
        if (running) return;
        running = true;
        last = performance.now();
        requestAnimationFrame(frame);
      },
      sleep(ms, mine) {
        if (mine !== generation) return Promise.reject(new Interrupted());
        return new Promise((resolve, reject) => waits.add({ until: clock + ms, resolve, reject }));
      },
      // Where to put the fingertip on a button: about the middle, a little low, so the finger doesn't hide much of the label.
      // A button that's gone from the page, or not laid out, has no box, and would send the hand to the top left corner.
      center(element) {
        const box = element.getBoundingClientRect();
        if (!box.width || !box.height) return null;
        return { x: box.left + box.width * style.aim.x, y: box.top + box.height * style.aim.y };
      },
      // Where to wait while waving at a button.
      waveSpot(element) {
        const point = api.center(element);
        return point && { x: point.x + style.aim.waveX, y: point.y + style.aim.waveY };
      },
      // Puts the hand somewhere straight away, without moving there.
      jumpTo(point) {
        Object.assign(at, point);
        goal = { ...point };
        speed.x = speed.y = 0;
      },
      async moveTo(point, mine, ms = 650) {
        if (point) goal = { ...point };
        await api.sleep(ms, mine);
      },
      async leave(mine) {
        await api.moveTo({ x: innerWidth + 200, y: innerHeight + 260 }, mine, 700);
      },
      // Each tap takes `each` milliseconds: a press, the lines as it lands, and back up.
      async tapTap(mine, times = 2, each = 220) {
        try {
          for (let i = 0; i < times; i++) {
            tap = 1;
            await api.sleep(each * 0.4, mine);
            burst();
            tap = 0;
            await api.sleep(each * 0.6, mine);
          }
        } finally {
          tap = 0;
        }
      },
      async shakeHead(mine, ms = 700) {
        shake = 1;
        try {
          await api.sleep(ms, mine);
        } finally {
          shake = 0;
        }
      },
      // Swaps to the open hand and waves it from the wrist, then goes back to pointing. Amount makes it a smaller wave.
      async wave(mine, ms = 1400, amount = 1) {
        pose = 1;
        wave = amount;
        if (waveEnv < 0.05) wavePhase = 0;
        try {
          await api.sleep(ms, mine);
        } finally {
          // The wave settles on its own while the hand turns back to pointing.
          wave = 0;
          pose = 0;
        }
      },
      // Draws the loop with the fingertip as it goes, so the ink comes out of the finger. A third of the time gets there.
      async circle(element, mine, ms = 1240) {
        const box = element.getBoundingClientRect();
        if (!box.width || !box.height) return;
        const points = loopAround(box, Math.random());
        scribble.setAttribute("d", points.map((p, i) => `${i ? "L" : "M"}${p.x.toFixed(1)} ${p.y.toFixed(1)}`).join(""));
        const length = scribble.getTotalLength();
        inkFade = null;
        scribble.style.strokeDasharray = `${length}`;
        scribble.style.strokeDashoffset = `${length}`;
        scribble.setAttribute("opacity", "1");
        await api.moveTo(points[0], mine, ms * 0.34);
        drawing = true;
        const span = ms * 0.66, began = clock;
        try {
          await new Promise((resolve, reject) => {
            const step = () => {
              if (mine !== generation || !running) return reject(new Interrupted());
              const t = Math.min((clock - began) / span, 1), eased = easeInOut(t);
              const index = eased * (points.length - 1);
              const a = points[Math.floor(index)], b = points[Math.min(Math.ceil(index), points.length - 1)], f = index % 1;
              goal = { x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f };
              scribble.style.strokeDashoffset = `${length * (1 - eased)}`;
              if (t < 1) requestAnimationFrame(step);
              else resolve();
            };
            requestAnimationFrame(step);
          });
        } finally {
          drawing = false;
        }
      },
      // Fades the loop out on its own, while the hand carries on.
      fadeInk(ms = 400) {
        const from = Number(scribble.getAttribute("opacity"));
        if (from > 0) inkFade = { began: clock, from, ms: Math.max(ms, 1) };
      },
      // For reduced motion: the hand just points, with the loop already drawn.
      pointStill(element) {
        const point = api.center(element);
        hand.style.transform = `translate(${point.x}px, ${point.y}px) rotate(${held(point.x, point.y)}deg) scale(${size})`;
        const points = loopAround(element.getBoundingClientRect(), 0.3);
        scribble.setAttribute("d", points.map((p, i) => `${i ? "L" : "M"}${p.x} ${p.y}`).join(""));
        scribble.setAttribute("opacity", "1");
      },
      remove() {
        running = false;
        api.interrupt();
        hand.remove();
        paper.remove();
      },
    };
    return api;
  }

  globalThis.asideLateHand = { create, Interrupted, STYLE };
})();
