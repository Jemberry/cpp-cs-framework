const API_BASE_URL = '/api/v1';

/**
 * Common request wrapper for JSON requests
 */
async function apiRequest(endpoint, method, data = null) {
  const headers = {
    'Content-Type': 'application/json',
  };

  const token = localStorage.getItem('accessToken');
  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }

  const options = {
    method,
    headers,
  };

  if (data) {
    options.body = JSON.stringify(data);
  }

  try {
    const response = await fetch(`${API_BASE_URL}${endpoint}`, options);
    const result = await response.json();

    if (!response.ok) {
      throw new Error(result.message || `Request failed with status ${response.status}`);
    }

    return result;
  } catch (error) {
    console.error('API Error:', error);
    throw error;
  }
}

/**
 * Request wrapper for file upload (multipart/form-data)
 */
async function uploadRequest(endpoint, formData) {
  const token = localStorage.getItem('accessToken');
  const headers = {};
  
  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }
  // Do NOT set Content-Type header; browser will set it with boundary

  const options = {
    method: 'POST',
    headers,
    body: formData,
  };

  try {
    const response = await fetch(`${API_BASE_URL}${endpoint}`, options);
    const result = await response.json();

    if (!response.ok) {
      throw new Error(result.message || `Upload failed with status ${response.status}`);
    }

    return result;
  } catch (error) {
    console.error('Upload API Error:', error);
    throw error;
  }
}

/**
 * Download file as blob
 * @param {number} fileId - File ID to download
 * @returns {Promise<Blob>} File blob with filename from Content-Disposition header
 */
async function downloadFile(fileId) {
  const token = localStorage.getItem('accessToken');
  const headers = {};
  
  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }

  try {
    const response = await fetch(`${API_BASE_URL}/file/${fileId}`, {
      method: 'GET',
      headers,
    });

    if (!response.ok) {
      const error = await response.json().catch(() => ({}));
      throw new Error(error.message || `Download failed with status ${response.status}`);
    }

    // Extract filename from Content-Disposition header if present
    const contentDisposition = response.headers.get('Content-Disposition');
    let filename = null;
    if (contentDisposition) {
      const filenameMatch = contentDisposition.match(/filename[^;=\n]*=((['"]).*?\2|[^;\n]*)/);
      if (filenameMatch && filenameMatch[1]) {
        filename = filenameMatch[1].replace(/['"]/g, '');
      }
    }

    const blob = await response.blob();
    return { blob, filename, contentType: response.headers.get('Content-Type') };
  } catch (error) {
    console.error('Download API Error:', error);
    throw error;
  }
}

/**
 * Authentication API methods
 */
export const authApi = {
  register: (data) => apiRequest('/auth/register', 'POST', data),
  login: (data) => apiRequest('/auth/login', 'POST', data),
};

/**
 * User API methods
 */
export const userApi = {
  getUserProfile: () => apiRequest('/user/me', 'GET'),
  
  /**
   * 获取当前用户的文件列表
   * @param {Object} params - 查询参数
   * @param {number} params.page - 页码，默认 1
   * @param {number} params.limit - 每页数量，默认 20
   * @returns {Promise} 返回文件列表及分页信息
   */
  getUserFiles: (params = {}) => {
    // const { page = 1, limit = 15 } = params;
    // const queryString = new URLSearchParams({
    //   page: String(page),
    //   limit: String(limit),
    // }).toString();
    // return apiRequest(`/files?${queryString}`, 'GET');
    return apiRequest('/files', 'GET');
  },

  /**
   * 上传文件
   * @param {File} file - 要上传的文件对象
   * @returns {Promise} 返回上传结果，包含 fileId, filename, size, createdAt
   * 
   * @example
   * const fileInput = document.getElementById('fileInput');
   * const file = fileInput.files[0];
   * const result = await userApi.uploadFile(file);
   * console.log('上传成功，文件ID:', result.data.fileId);
   */
  uploadFile: async (file) => {
    const formData = new FormData();
    formData.append('file', file);
    return uploadRequest('/files', formData);
  },

  /**
   * 下载文件
   * @param {number} fileId - 文件ID
   * @returns {Promise<{blob: Blob, filename: string|null, contentType: string|null}>} 
   *          返回文件Blob及原始文件名
   * 
   * @example
   * const { blob, filename } = await userApi.downloadFile(1);
   * // 触发浏览器下载
   * const url = URL.createObjectURL(blob);
   * const a = document.createElement('a');
   * a.href = url;
   * a.download = filename || 'download';
   * a.click();
   * URL.revokeObjectURL(url);
   */
  downloadFile: (fileId) => downloadFile(fileId),
};

/**
 * 辅助函数：触发浏览器下载（可用于组件中复用）
 * @param {Blob} blob - 文件Blob
 * @param {string} filename - 下载时的文件名
 */
export function triggerDownload(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}
