// src/soma/math.rs

/// Compute the dot product of two f32 slices.
/// Ideally this would be SIMD, but we start scalar for safety.
pub fn dot_product(a: &[f32], b: &[f32]) -> f32 {
    let len = core::cmp::min(a.len(), b.len());
    let mut sum = 0.0;
    for i in 0..len {
        sum += a[i] * b[i];
    }
    sum
}

/// Compute matrix-vector multiplication: y = W * x
/// W is flattened row-major (rows x cols).
/// x is vector len (cols).
/// y is vector len (rows).
pub fn mat_vec_mul(w: &[f32], x: &[f32], y: &mut [f32], rows: usize, cols: usize) {
    if w.len() < rows * cols || x.len() < cols || y.len() < rows {
        return; // Safety first
    }
    
    for r in 0..rows {
        let row_start = r * cols;
        let row = &w[row_start..row_start + cols];
        y[r] = dot_product(row, x);
    }
}
