/*
#include <iostream>
#include <locale>
#include <exception>
 
int main()
{
    try{
   
    std::wcout << "User-preferred locale setting is " << std::locale("").name().c_str() << '\n';
    // on startup, the global locale is the "C" locale
    std::wcout << 1000.01 << '\n';
    // replace the C++ global locale as well as the C locale with the user-preferred locale
    std::locale::global(std::locale("C.fr_FR.utf-8"));

    std::wcout << "User-preferred locale setting is " << std::locale("").name().c_str() << '\n';
    // use the new global locale for future wide character output
    std::wcout.imbue(std::locale());
    // output the same number again
    std::wcout << 1000.01 << '\n';
    }catch( std::exception& e)
    {
        std::cout<< e.what() << std::endl;
    }
}
*/

#include <iostream>
#include <string>
#include <cmath>
#include <memory>
#include <complex>
#include <algorithm>
#include <array>
#include <numeric>
#include <type_traits>

/*
template<typename  vector_type>
auto  operator*(const vector_type& vect0, const vector_type& vect1)
{
    vector_type ret;
    std::transform(vect1.begin(), vect1.end(),vect0.begin(), ret.begin(), std::multiply<fp_type>());
    return stdt::move(ret);
}

template<typename  vector_type, typename fp_type >
auto  operator+(vector_type& vect0, vector_type& vect1)
{
    vector_type ret;
    std::transform(vect1.begin(), vect1.end(),vect0.begin(), ret.begin(), std::plus<fp_type>());
    return stdt::move(ret);
}

template<typename  vector_type, typename fp_type >
auto  operator-(vector_type& vect0, vector_type& vect1)
{
    vector_type ret;
    std::transform(vect1.begin(), vect1.end(),vect0.begin(), ret.begin(), std::minus<fp_type>());
    return stdt::move(ret);
}

*/

template <class fp_type>
struct vect3
{

    std::array<fp_type, 3> _array;

    vect3(vect3<fp_type> &v) : _array(v._array) {}
    vect3(vect3<fp_type> &&v) : _array(v._array) {}
    vect3() : _array({0, 0, 0}){};

  template <typename = typename std::enable_if<std::is_floating_point<scalar_type>::value, vect3<fp_type>>::type>
   operator*(const scalar_type& scalar)
    {
        vect3<fp_type> ret;
        ret._array = {
            _array[0] * scalar,
            _array[1] * scalar,
            _array[2] * scalar
            };
        return std::move(ret);
    }
    
 vect3<fp_type> mul(const scalar_type &vect1, std::true_type) 
  {
         vect3<fp_type> ret;
        ret._array = {
            _array[0] * scalar,
            _array[1] * scalar,
            _array[2] * scalar
            };
        return std::move(ret);
  } 

 fp_type mul(const vect3<fp_type> &vect1, std::false_type) 
  {
        vect3<fp_type> ret;
        std::transform(vect1._array.begin(), vect1._array.end(), _array.begin(), ret._array.begin(), std::multiplies<fp_type>());
        fp_type init;
        return std::accumulate(ret._array.begin(), ret._array.end(), init, std::plus<fp_type>());
  } 

    vect3<fp_type> operator-(const vect3<fp_type> &vect1)
    {
        vect3<fp_type> ret;
        std::transform(vect1._array.begin(), vect1._array.end(), _array.begin(), ret._array.begin(), std::minus<fp_type>());
        return std::move(ret);
    }

    vect3<fp_type> operator^(const vect3<fp_type> &vect1)
    {
        vect3<fp_type> product;
        product._array =
        { _array[1] * vect1[2] - _array[2] * vect1[1],
          _array[2] * vect1[0] - _array[0] * vect1[2],
          _array[0] * vect1[1] - _array[1] * vect1[0]
        };
        return std::move(product);
    }

    vect3<fp_type> operator+(const vect3<fp_type> &vect1)
    {
        vect3<fp_type> ret;
        std::transform(vect1._array.begin(), vect1._array.end(), _array.begin(), ret._array.begin(), std::plus<fp_type>());
        return std::move(ret);
    }

    vect3<fp_type> &operator=(const vect3<fp_type> &vect1)
    {
        _array[0] = vect1[0];
        _array[1] = vect1[1];
        _array[2] = vect1[2];
        return *this;
    }

    fp_type operator[](const int index)
    {
        return _array[index];
    }
};

template <typename fp_type>
struct Quaternion
{

    Quaternion() = default;
    Quaternion(fp_type a, vect3<std::complex<fp_type>> &b) : w(a), v(b) {}
    Quaternion(Quaternion<fp_type> &q) : w(std::move(q.w)), v(std::move(q.v)) {}
    Quaternion(Quaternion<fp_type> &&q) : w(q.w), v(q.v) {}

    Quaternion<fp_type> operator*(const Quaternion<fp_type> &q)
    {
        /* a_=(a_*q.a_) -(b_.real()*q.b_.real())-(c_.real()*q.c_.real())-(d_.real()*q.d_.real());
    b_= ( (a_*q.b_.real()) + (b_.real()*q.a_) + (c_.real()*q.d_.real()) -(d_.real()*q.c_.real())  );
    c_=( (a_*q.c_.real()) - (b_.real()*q.d_.real()) + (c_.real()*q.a_) + (d_.real() * b_.real()));
    d_=( (a_*q.d_.real()) +(b_.real()*q.c_.real()) -(c_.real()*q.b_.real()) +(d_.real()*q.a_)  );
    */
        Quaternion<fp_type> q1;
        q1.w = w * q.w - (v * q.v).real();
        auto x_1 = v * q.w;
        auto x_2 = q.v * w;
        auto x_3 = (q.v ^ v);
        q1.v = (x_1 + x_2) + x_3;
        return std::move(q1);
    }

    Quaternion<fp_type> &operator=(const Quaternion<fp_type> &q)
    {
        w = q.w;
        v = q.v;
        return *this;
    }

    fp_type w;
    vect3<std::complex<fp_type>> v;
};

int main()
{
    vect3<std::complex<float>> v;
    v._array = {1.75f, -4.f, -5.f};
    Quaternion<float> q1(3.f, v);
    auto prod = (q1 * q1);
    std::cout << "prod.w = " << prod.w << "prod.x = " << prod.v[0] << "prod.y = " << prod.v[1] << "prod.z = " << prod.v[1] << "!\n";
/*
 auto xs=v*v;
 float a=3.2;
 auto xv= v*a;
 */
    return 0;
}
